#include "runtime/session/detail/RuntimeProductionOperations.h"

namespace concord::runtime::session_detail {

std::string runtimeProjectileLifecycleName(
    RuntimeProjectileLifecycleState state) {
    switch (state) {
    case RuntimeProjectileLifecycleState::Spawned:
        return "Spawned";
    case RuntimeProjectileLifecycleState::Traveling:
        return "Traveling";
    case RuntimeProjectileLifecycleState::Impacting:
        return "Impacting";
    case RuntimeProjectileLifecycleState::Expired:
        return "Expired";
    }
    return "Expired";
}

RuntimeProjectileTargetPosition runtimeProjectileTargetPosition(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    RuntimeProjectileTargetPosition target;
    target.x = runtimeObjectRenderX(object);
    target.y = runtimeObjectRenderY(object);
    target.level = static_cast<int>(std::llround(
        runtimeDrawLevelForObject(object)
    ));
    if (object.kind == WorldObjectKind::Structure) {
        const RuntimeStructureFootprint footprint =
            runtimeStructureFootprintForObject(state, object);
        target.x += static_cast<double>(
            std::max(1, footprint.width) - 1
        ) * 0.5;
        target.y += static_cast<double>(
            std::max(1, footprint.height) - 1
        ) * 0.5;
    }
    target.cell = {
        static_cast<int>(std::llround(target.x)),
        static_cast<int>(std::llround(target.y))
    };
    return target;
}

RuntimeCellPoint runtimeTargetCellForProjectile(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    return runtimeProjectileTargetPosition(state, object).cell;
}

int runtimeStructureDepartureHostIdReference(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover) {
    if (!runtimeObjectMayUseStructureDriveRows(state, mover)) return -1;
    const int key = runtimeCellKey(mover.x, mover.y);
    if (state.tickOccupancyCacheTick == state.tick) {
        const auto count = state.tickGroundOccupantCountByCell.find(key);
        const auto occupant = state.tickOccupiedByCell.find(key);
        if ((count == state.tickGroundOccupantCountByCell.end() || count->second <= 1)
            && (occupant == state.tickOccupiedByCell.end() || occupant->second == mover.instanceId))
            return -1;
    }
    int hostId = -1;
    for (const auto& structure : state.world.getObjects()) {
        if (structure.alive && structure.kind == WorldObjectKind::Structure
            && runtimeStructureCellIsVehicleDriveRow(state, structure,
                runtimeCellPointForObject(mover), mover)
            && (hostId < 0 || structure.instanceId < hostId)) hostId = structure.instanceId;
    }
    return hostId;
}

int runtimeStructureDepartureHostId(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover) {
    if (!runtimeHistoryQueryReference() && runtimeActiveBuildingDepartureReadOnlyQueries != nullptr) {
        if (const auto cached = runtimeActiveBuildingDepartureReadOnlyQueries->find(state, mover))
            return *cached;
    }
    return runtimeStructureDepartureHostIdReference(state, mover);
}

bool runtimeObjectHasAttackAmmo(
    const RulesDatabase& rules,
    const WorldObjectInstance& object) {
    const UnitType* unit = runtimeAmmoRulesForObject(rules, object);
    return unit == nullptr || unit->ammo < 0 || object.currentAmmo > 0;
}

const VeterancyProfile* runtimeVeterancyProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (const UnitType* unit = findRuntimeUnitType(state.rules, object)) {
        return &unit->veterancy;
    }
    if (const BuildingType* building =
            findRuntimeBuildingType(state.rules, object)) {
        return &building->veterancy;
    }
    return nullptr;
}

bool runtimeVeterancyHasAbility(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    VeteranAbility ability) {
    if (object.veterancy <= 0) {
        return false;
    }
    const VeterancyProfile* profile =
        runtimeVeterancyProfileForObject(state, object);
    if (profile == nullptr) {
        return false;
    }
    const std::vector<VeteranAbility>& abilities =
        object.veterancy >= 2
            ? profile->eliteAbilities
            : profile->veteranAbilities;
    return std::find(abilities.begin(), abilities.end(), ability)
        != abilities.end();
}

double runtimeVeterancyAbilityMultiplier(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    VeteranAbility ability) {
    const VeterancyRules& rules = state.rules.getVeterancyRules();
    const bool hasVeteranAbility =
        runtimeVeterancyHasAbility(state, object, ability);
    double multiplier = 1.0;
    switch (ability) {
    case VeteranAbility::Faster:
        multiplier = (hasVeteranAbility ? rules.veteranSpeed : 1.0)
            * object.crateSpeedMultiplier * runtimeAttachMultiplier(object, state.rules, &AttachEffectRule::speed);
        break;
    case VeteranAbility::Stronger:
        multiplier = (hasVeteranAbility ? rules.veteranArmor : 1.0)
            * object.crateArmorMultiplier * runtimeAttachMultiplier(object, state.rules, &AttachEffectRule::armor);
        break;
    case VeteranAbility::Firepower:
        multiplier = (hasVeteranAbility ? rules.veteranCombat : 1.0)
            * object.crateFirepowerMultiplier * runtimeAttachMultiplier(object, state.rules, &AttachEffectRule::firepower)
            * (object.tankBunkerHostId >= 0
                ? state.rules.getSpecialMechanismRules().bunkerDamageMultiplier : 1.0);
        break;
    case VeteranAbility::Rof:
        multiplier = (hasVeteranAbility ? rules.veteranRof : 1.0) * runtimeAttachMultiplier(object, state.rules, &AttachEffectRule::rof);
        if (object.tankBunkerHostId >= 0
            && state.rules.getSpecialMechanismRules().bunkerRofMultiplier > 0.0) {
            multiplier /= state.rules.getSpecialMechanismRules().bunkerRofMultiplier;
        }
        break;
    case VeteranAbility::Sight:
        multiplier = hasVeteranAbility ? rules.veteranSight : 1.0;
        break;
    default:
        break;
    }
    return std::clamp(multiplier, 0.01, 100.0);
}

int runtimeVeterancyObjectCost(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (const UnitType* unit = findRuntimeUnitType(state.rules, object)) {
        return std::max(0, unit->cost);
    }
    if (const BuildingType* building =
            findRuntimeBuildingType(state.rules, object)) {
        return std::max(0, building->cost);
    }
    return 0;
}

bool runtimeVeterancyObjectIsTrainable(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const VeterancyProfile* profile =
        runtimeVeterancyProfileForObject(state, object);
    return profile != nullptr && profile->trainable;
}

bool runtimeVeterancyObjectAwardsExperience(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (const UnitType* unit = findRuntimeUnitType(state.rules, object)) {
        return !unit->dontScore && !unit->insignificant;
    }
    if (const BuildingType* building =
            findRuntimeBuildingType(state.rules, object)) {
        return !building->dontScore && !building->insignificant;
    }
    return false;
}

bool runtimeVeterancyTypeListContains(
    const std::vector<std::string>& typeIds,
    const std::string& typeId) {
    const std::string wanted =
        toLowerAsciiLocal(trimAsciiLocal(typeId));
    return !wanted.empty()
        && std::any_of(
            typeIds.begin(),
            typeIds.end(),
            [&wanted](const std::string& candidate) {
                return toLowerAsciiLocal(trimAsciiLocal(candidate)) == wanted;
            }
        );
}

bool runtimeCountryGrantsVeterancy(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (!runtimeVeterancyObjectIsTrainable(state, object)) {
        return false;
    }
    const CountryDefinition* country = state.rules.findCountryDefinition(
        runtimeCountryForOwner(state, object.owner)
    );
    if (country == nullptr) {
        return false;
    }
    if (object.kind == WorldObjectKind::Infantry) {
        return runtimeVeterancyTypeListContains(
            country->veteranInfantry,
            object.type
        );
    }
    if (object.authoredAircraft) {
        return runtimeVeterancyTypeListContains(
            country->veteranAircraft,
            object.type
        );
    }
    if (state.rules.findUnitType("UnitType", object.type) != nullptr) {
        return runtimeVeterancyTypeListContains(
            country->veteranUnits,
            object.type
        );
    }
    return false;
}

void applyRuntimeProducedObjectVeterancy(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& object) {
    std::optional<RuntimeSpyVeteranProduction> spyProduction;
    if (object.kind == WorldObjectKind::Infantry) {
        spyProduction = RuntimeSpyVeteranProduction::Infantry;
    } else if (object.authoredAircraft) {
        spyProduction = RuntimeSpyVeteranProduction::Aircraft;
    } else if (object.kind == WorldObjectKind::Unit
        && state.rules.findUnitType("UnitType", object.type) != nullptr) {
        spyProduction = RuntimeSpyVeteranProduction::Vehicles;
    }
    const bool spyVeteranProduction = spyProduction.has_value()
        && state.spies.hasVeteranProduction(
            runtimeRepairDepotCanonicalOwnerId(state, object.owner),
            *spyProduction
        );
    if (object.veterancy >= 1
        || !runtimeVeterancyObjectIsTrainable(state, object)
        || (!runtimeCountryGrantsVeterancy(state, object)
            && !spyVeteranProduction)) {
        return;
    }
    const bool couldCloakBefore = runtimeObjectCanCloak(state, object);
    object.veterancy = 1;
    object.veterancyExperience = 0;
    if (!couldCloakBefore && runtimeObjectCanCloak(state, object)) {
        initializeRuntimeObjectCloakState(state, object);
    }
}

void applyRuntimeInitialObjectVeterancy(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& object) {
    const VeterancyRules& rules = state.rules.getVeterancyRules();
    if (!runtimeVeterancyObjectIsTrainable(state, object)) {
        return;
    }
    if (rules.initialVeteran || state.mapInitialVeteran) {
        // InitialVeteran is the original full-veterancy special flag/cheat.
        // It grants Elite directly even if normal XP promotion is capped
        // lower. A map SpecialFlags value can enable it, but an absent/false
        // value never disables a rules-level true value.
        const bool couldCloakBefore = runtimeObjectCanCloak(state, object);
        object.veterancy = 2;
        object.veterancyExperience = 0;
        if (!couldCloakBefore && runtimeObjectCanCloak(state, object)) {
            initializeRuntimeObjectCloakState(state, object);
        }
        return;
    }
    applyRuntimeProducedObjectVeterancy(state, object);
}

const WeaponType* findRuntimeWeaponType(
    const RulesDatabase& rules,
    const std::string& weaponId) {
    if (weaponId.empty()) {
        return nullptr;
    }
    return rules.findWeaponType(weaponId);
}

const ProjectileType* findRuntimeProjectileType(
    const RulesDatabase& rules,
    const std::string& projectileId) {
    if (projectileId.empty()) {
        return nullptr;
    }
    return rules.findProjectileType(projectileId);
}

const WarheadType* findRuntimeWarheadType(
    const RulesDatabase& rules,
    const std::string& warheadId) {
    if (warheadId.empty()) {
        return nullptr;
    }
    return rules.findWarheadType(warheadId);
}

double runtimeWarheadArmorMultiplier(
    const WarheadType* warhead,
    const std::string& armor) {
    if (warhead == nullptr || warhead->verses.empty()) {
        return 1.0;
    }

    // The text owns the cache key, so rule reloads and edits on the same
    // WarheadType cannot reuse stale values or retain dangling pointers.
    static thread_local std::unordered_map<std::string, std::vector<double>>
        parsedVersesByText;
    auto cached = parsedVersesByText.find(warhead->verses);
    if (cached == parsedVersesByText.end()) {
        constexpr std::size_t MaxCachedVerses = 128;
        if (parsedVersesByText.size() >= MaxCachedVerses) {
            parsedVersesByText.clear();
        }
        cached = parsedVersesByText.emplace(
            warhead->verses, parseRuntimeVerses(warhead->verses)
        ).first;
    }
    // No other lookup or insertion occurs while this reference is in use.
    const std::vector<double>& verses = cached->second;
    const int armorIndex = runtimeArmorIndex(armor);
    if (armorIndex < 0
        || static_cast<std::size_t>(armorIndex) >= verses.size()) {
        return 1.0;
    }

    return verses[static_cast<std::size_t>(armorIndex)];
}

std::string chooseRuntimeImpactAnim(
    const WarheadType* warhead,
    const WeaponType& weapon) {
    if (warhead != nullptr) {
        const std::vector<std::string> anims =
            splitRuntimeTokenList(warhead->animList);
        if (!anims.empty()) {
            if (toUpperAsciiLocal(weapon.projectile) == "BLIMPBOMBP"
                && anims.size() > 1) {
                return anims.back();
            }
            return anims.front();
        }
    }

    return "";
}

void ensureRuntimeGarrisonsInitialized(RuntimeGameSessionState& state) {
    if (state.garrisonsInitialized || !state.rulesLoaded) {
        return;
    }
    RuntimeGarrisonConfig config;
    const SpecialMechanismRules& rules =
        state.rules.getSpecialMechanismRules();
    config.conditionRedFraction = rules.conditionRed;
    config.occupyDamageMultiplier = rules.occupyDamageMultiplier;
    config.occupyRofMultiplier = rules.occupyRofMultiplier;
    config.occupyWeaponRange = static_cast<double>(rules.occupyWeaponRange);
    config.fixedTickHz = std::max(1, state.config.fixedTickHz);
    state.garrisons.reset(config);
    state.garrisonsInitialized = true;
}

void ensureRuntimeSuperWeaponsInitialized(RuntimeGameSessionState& state) {
    if (state.superWeaponsInitialized || !state.rulesLoaded) {
        return;
    }
    state.superWeapons.reset(state.rules);
    state.superWeaponsInitialized = true;
}

std::vector<std::array<int, 4>> runtimeParasiteLinkedHostPoses(
    RuntimeGameSessionState& state) {
    ensureRuntimeSpecialMechanismsInitialized(state);
    std::vector<std::array<int, 4>> poses;
    poses.reserve(state.specialMechanisms.parasiteLinks().size());
    for (const auto& [targetId, link] :
            state.specialMechanisms.parasiteLinks()) {
        (void)link;
        const WorldObjectInstance* target = findObjectById(state, targetId);
        if (target != nullptr && target->alive) {
            poses.push_back({targetId, target->x, target->y, target->level});
        }
    }
    return poses;
}

bool notifyRuntimeParasiteHostsRelocated(
    RuntimeProductionHostState& state,
    const std::vector<std::array<int, 4>>& before) {
    bool changed = false;
    for (const std::array<int, 4>& pose : before) {
        const WorldObjectInstance* target = findObjectById(state, pose[0]);
        if (target == nullptr || !target->alive
            || (target->x == pose[1] && target->y == pose[2]
                && target->level == pose[3])
            || state.specialMechanisms.parasiteLinks().find(pose[0])
                == state.specialMechanisms.parasiteLinks().end()) {
            continue;
        }
        changed = handleRuntimeParasiteHostEvent(
            state,
            pose[0],
            ParasiteHostEventKind::Teleporting,
            std::max(1, state.config.fixedTickHz)
        ) || changed;
    }
    return changed;
}

SuperWeaponWorldContext runtimeSuperWeaponContext(RuntimeProductionHostState& state) {
    SuperWeaponWorldContext context;
    context.tick = state.tick;
    context.fixedTickHz = std::max(1, state.config.fixedTickHz);
    // Width/height describe the diamond, not its cell-coordinate bounds.
    context.mapWidth = std::max(0, state.world.getSummary().maxCellX + 1);
    context.mapHeight = std::max(0, state.world.getSummary().maxCellY + 1);
    context.animationDurationTicks = [&state](const std::string& id) {
        return runtimeSpecialDetailAnimationDurationTicks(state, id);
    };
    context.animationMidpointTicks = [&state](const std::string& id) {
        return runtimeSpecialDetailAnimationMidpointTicks(state, id);
    };
    context.damageImmune = [&state](const WorldObjectInstance& object) {
        if (runtimeCarrierAircraftHasDeckProtection(state, object)) return true;
        if (state.specialMechanisms.isTemporalFrozen(object.instanceId)) return true;
        if (const auto* unit = findRuntimeUnitType(state.rules, object)) return unit->immune;
        if (const auto* building = findRuntimeBuildingType(state.rules, object)) return building->immune;
        return false;
    };
    context.superWeaponsAllowed = state.config.superweapons;
    context.noCooldown = [&state](const std::string& owner) {
        return runtimeNoCooldownForOwner(state, owner);
    };
    context.objects = &state.world.accessObjects();
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.geneticMutationMaxHealth = [&state](const WorldObjectInstance& replacement) {
        return runtimeMaxHealthForObject(state, replacement);
    };
    context.areAllied = [&state](const std::string& left, const std::string& right) {
        return runtimeOwnersAreAllied(state, left, right);
    };
    context.cellLegal = [&state](int x, int y, SuperWeaponTargetMode mode) {
        (void)mode;
        if (x < 0 || y < 0) {
            return false;
        }
        if (!state.terrainByCell.empty()) {
            return state.terrainByCell.find(runtimeCellKey(x, y))
                != state.terrainByCell.end();
        }
        return std::any_of(
            state.world.getTerrainCells().begin(),
            state.world.getTerrainCells().end(),
            [x, y](const TerrainCell& cell) { return cell.x == x && cell.y == y; }
        );
    };
    context.cellOccupied = [&state](int x, int y) {
        return std::any_of(
            state.world.getObjects().begin(),
            state.world.getObjects().end(),
            [x, y](const WorldObjectInstance& object) {
                return object.alive && object.x == x && object.y == y;
            }
        );
    };
    context.explored = [&state](const std::string& owner, int x, int y) {
        return !state.visionEnabled
            || state.vision.isCellExploredBy(owner, x, y);
    };
    context.ownerPowered = [&state](const std::string& owner) {
        if (runtimeDiskDrainOwnerBlackout(state, owner)) return false;
        if (state.superWeapons.isOwnerBlackout(owner, state.tick)) {
            return false;
        }
        if (state.spies.blackoutUntilTick(
                runtimeRepairDepotCanonicalOwnerId(state, owner)
            ) > state.tick) {
            return false;
        }
        int produced = 0;
        int used = 0;
        const std::string ownerKey = toUpperAsciiLocal(trimAsciiLocal(owner));
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive
                || toUpperAsciiLocal(trimAsciiLocal(object.owner)) != ownerKey
                || object.kind != WorldObjectKind::Structure) {
                continue;
            }
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
        }
        return produced - used >= 0;
    };
    context.ownerCountry = [&state](const std::string& owner) { return runtimeCountryForOwner(state, owner); };
    context.terrainLevel = [&state](int x, int y) { return terrainLevelAtRuntimeCell(state, x, y); };
    context.nuclearLaunchCell = [&state](const WorldObjectInstance& source) {
        const auto cell = runtimePlacementAnchorCellForFootprint(
            {static_cast<int>(std::lround(runtimeObjectRenderX(source))),
                static_cast<int>(std::lround(runtimeObjectRenderY(source)))},
            runtimeStructureFootprintForObject(state, source));
        return SuperWeaponCell{cell.x, cell.y};
    };
    context.facingForDelta = [](int dx, int dy) { return runtimeFacingFromDelta(dx, dy); };
    context.canPlaceObject = [&state](const WorldObjectInstance& object, int x, int y) {
        return runtimeSupportPowerCanPlace(state, object, x, y);
    };
    context.chronoDestinationIsFatalWater = [&state](const WorldObjectInstance& object, int x, int y) {
        return runtimeSupportPowerFatalWaterDestination(state, object, x, y);
    };
    context.paratrooperSubcell = [&state](const WorldObjectInstance& object, int x, int y,
            double desiredX, double desiredY) {
        return runtimeSupportPowerParatrooperSubcell(state, object, x, y, desiredX, desiredY);
    };
    context.onObjectDamaged = context.onObjectRelocated = context.onObjectOwnerChanged =
        [&state](SuperWeaponObjectLifecycleEvent event) {
            state.pendingSuperWeaponObjectEvents.push_back(std::move(event));
        };
    context.onSuperWeaponImpact = [&state](SuperWeaponImpactEvent event) {
        state.pendingSuperWeaponImpacts.push_back(std::move(event));
    };
    context.aiTargetVisible = [&state](
        const std::string& owner,
        const WorldObjectInstance& object
    ) {
        return !runtimeObjectHiddenFromOwnerByStealth(state, owner, object)
            && !runtimeSpyDisguiseConcealsFromOwner(
                state,
                owner,
                object
            );
    };
    context.allocateObjectId = [&state]() {
        int nextId = 0;
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            nextId = std::max(nextId, object.instanceId);
        }
        return nextId + 1;
    };
    context.onObjectCreated = [&state](WorldObjectInstance& object) {
        initializeRuntimeObjectCloakState(state, object);
        initializeRuntimeSpyDefaultDisguiseForObject(
            state,
            object.instanceId
        );
        applyRuntimeProducedObjectVeterancy(state, object);
        state.cloakObservedHealthByObject[object.instanceId] = object.health;
        state.projectedObjectsCacheValid = false;
        state.repaintRequested = true;
        if (!object.airborne) state.navigationRefreshPending = true;
    };
    context.onSuperWeaponActiveLifecycle = [&state](
        SuperWeaponActiveLifecycleEvent event
    ) {
        applyRuntimeSuperWeaponLightingLifecycle(state, event);
    };
    context.onObjectRemoved = [&state](
        SuperWeaponObjectLifecycleEvent event
    ) {
        if (event.kind != SuperWeaponObjectLifecycleKind::Removed
            || event.removalDisposition
                == SuperWeaponObjectRemovalDisposition::None) {
            return;
        }
        // The chrono engine has already marked a fatal arrival dead, so the
        // queued relocation observer will skip it. Preserve its actual landing
        // layer and canonical occupancy key before the common death commits.
        if (event.reason == "chronosphere fatal water destination") {
            if (auto* landed = findMutableRuntimeObjectByIdIncludingInactive(state, event.objectId)) {
                landed->cell = runtimeCellKey(landed->x, landed->y);
                landed->level = terrainLevelAtRuntimeCell(state, landed->x, landed->y);
                landed->onBridge = landed->renderOnBridge = false;
                landed->hasRenderPosition = landed->renderMoveActive = false;
            }
        }
        RuntimeObjectExitRequest request;
        request.objectId = event.objectId;
        request.tick = event.tick;
        request.sourceObjectId = event.superWeaponInstanceId;
        request.sourceOwner = event.sourceOwner;
        request.warheadId = event.warheadId;
        request.wasAliveBeforeDamage = event.healthBefore > 0;
        request.reason = event.reason.empty()
            ? "superweapon object removed"
            : event.reason;
        if (event.removalDisposition
                == SuperWeaponObjectRemovalDisposition::TransformSource) {
            request.kind = RuntimeObjectExitKind::TransformSource;
            request.mission = "Transformed";
            request.suppressDeathWeapon = true;
        } else if (event.removalDisposition == SuperWeaponObjectRemovalDisposition::Consumed) {
            request.kind = RuntimeObjectExitKind::Consumed;
            request.mission = "SupportDeparted";
            request.suppressDeathWeapon = true;
        } else {
            request.kind = RuntimeObjectExitKind::Destroyed;
            request.mission = "Destroyed";
            request.unitDeathSoundRequested = event.healthBefore > 0;
        }
        (void)queueRuntimeObjectExit(state, std::move(request));
    };
    return context;
}

std::function<void(const SuperWeaponEvent&)> runtimeSuperWeaponEventSink(
    RuntimeProductionHostState& state) {
    return [&state](const SuperWeaponEvent& superEvent) {
        if (superEvent.kind == "IronCurtainProtected" || superEvent.kind == "ForceShieldProtected"
            || superEvent.kind == "GeneticMutationApplied" || superEvent.kind == "ChronoWarpIn"
            || superEvent.kind == "PsychicDominatorStarted" || superEvent.kind == "ChronoSphereApplied"
            || superEvent.kind == "SuperWeaponLaunched"
            || superEvent.kind == "SuperWeaponFirstTargetAccepted")
        {
            RuntimeSuperWeaponVisualEvent visual{superEvent, std::nullopt};
            if (superEvent.kind == "GeneticMutationApplied") {
                // The source is already marked inactive, but its exit is only
                // queued. Capture its real foot point before exit cleanup;
                // the newly created Brute only retains the logical cell.
                for (const auto& source : state.world.getObjects()) {
                    if (source.instanceId != superEvent.targetObjectId) continue;
                    const auto offset = worldObjectSubCellOffset(source);
                    visual.event.targetCell = {source.x, source.y};
                    visual.worldPosition = std::array<double, 3>{
                        runtimeObjectRenderX(source) + offset.x,
                        runtimeObjectRenderY(source) + offset.y,
                        runtimeDrawLevelForObject(source)};
                    // GENDEATH has its own centered animation canvas. Place
                    // that canvas at the old infantry's authored body pivot,
                    // using the existing native-resolution presentation rule.
                    RuntimeMapRenderContext context;
                    context.art = state.artLoaded ? &state.art : nullptr;
                    context.rules = state.rulesLoaded ? &state.rules : nullptr;
                    context.theater = state.theaterLoaded ? &state.theater : nullptr;
                    context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
                    context.assetRootPath = runtimeAssetRootPath(state.config);
                    context.palettePath = runtimePalettePath(state.config, state.report.map);
                    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
                    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
                    context.animationTick = state.tick;
                    context.fixedTickHz = state.config.fixedTickHz;
                    if (runtimeSessionGeometryAvailable(state)) (void)state.geometry->resolveRuntimeInfantryDeathCanvasPivotOffsets(source,
                        context, state.renderCache, visual.canvasOffsets[0],
                        visual.canvasOffsets[1], visual.canvasOffsets[2],
                        visual.canvasOffsets[3]);
                    break;
                }
            }
            state.pendingSuperWeaponVisuals.push_back(std::move(visual));
        }
        auto soundPosition = RuntimeSpatialAudioPosition{
            double(superEvent.targetCell.x), double(superEvent.targetCell.y),
            double(terrainLevelAtRuntimeCell(state, superEvent.targetCell.x, superEvent.targetCell.y))};
        if (superEvent.kind == "SpyPlaneAircraftLaunched" || superEvent.kind == "ParadropAircraftLaunched"
            || superEvent.kind == "SpyPlaneRevealArea" || superEvent.kind == "ParadropInfantryDropped") {
            const int sourceId = superEvent.soundSourceObjectId >= 0 ? superEvent.soundSourceObjectId : superEvent.objectId;
            if (const auto* object = findObjectById(state, sourceId))
                soundPosition = runtimeSpatialAudioPositionForObject(*object);

        }
        if (superEvent.kind == "ParadropInfantryDropped") {
            const auto sound = trimAsciiLocal(state.rules.getAudioVisualValue("ChuteSound"));
            if (!sound.empty()) emitRuntimeSound(state, "sound:" + sound, soundPosition, true);
        }
        if (superEvent.kind == "NukeProjectileLaunched") {
            const auto* rule = state.rules.findSuperWeaponType(superEvent.superWeaponId);
            const auto* source = findObjectById(state, superEvent.soundSourceObjectId);
            if (rule && source && source->alive) {
                const auto sound = runtimeArtAnimationSound(state, state.art.findArtType(rule->launchAnim));
                const auto cell = runtimePlacementAnchorCellForFootprint(
                    {static_cast<int>(std::lround(runtimeObjectRenderX(*source))),
                        static_cast<int>(std::lround(runtimeObjectRenderY(*source)))},
                    runtimeStructureFootprintForObject(state, *source));
                if (!sound.empty()) emitRuntimeSound(state, "sound:" + sound,
                    RuntimeSpatialAudioPosition{double(cell.x), double(cell.y), double(source->level)}, true);
            }
        }
        RuntimeReplayEventSnapshot event;
        event.kind = "SuperWeapon";
        event.superWeaponId = superEvent.superWeaponId;
        event.superWeaponEvent = superEvent.kind;
        event.objectId = superEvent.objectId;
        event.targetObjectId = superEvent.targetObjectId;
        event.commandVerb = superEvent.commandVerb;
        event.reason = superEvent.reason;
        event.lifecycleState = superEvent.state;
        event.commandTargetCell = {
            superEvent.targetCell.x,
            superEvent.targetCell.y
        };
        event.sourceCell = event.commandTargetCell;
        event.targetCell = event.commandTargetCell;
        event.secondTargetCell = {
            superEvent.targetCell2.x,
            superEvent.targetCell2.y
        };
        event.renderIntentId =
            "superweapon:" + superEvent.superWeaponId + ":"
            + superEvent.kind + ":" + superEvent.animationIntent;
        event.renderIntentHash = runtimeStableHashString(event.renderIntentId);
        recordRuntimeReplayEvent(state, std::move(event));
        state.simulationStatus =
            "superweapon " + superEvent.superWeaponId
            + " " + superEvent.kind;
        const std::string soundIntent = trimAsciiLocal(
            superEvent.soundIntent
        );
        const std::string evaIntent = trimAsciiLocal(superEvent.evaIntent);
        const bool localOwner = !state.remoteHumanHouseScopeActive
            && toLowerAsciiLocal(trimAsciiLocal(superEvent.owner))
                == toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
        if (!soundIntent.empty()) {
            const auto section = runtimeSoundSectionFromEvent(state, "sound:" + soundIntent);
            if (runtimeSoundSectionIsEva(state, section)) {
                if (localOwner) emitRuntimeSound(state, "sound:" + soundIntent);
            } else {
                const auto* spec = state.worldAudioCatalog.find(section);
                const bool planeLoop = spec && spec->loop
                    && (superEvent.kind == "SpyPlaneAircraftLaunched" || superEvent.kind == "ParadropAircraftLaunched");
                if (planeLoop) {
                    noteRuntimeSoundIntent(state, "sound:" + spec->eventId);
                    if (superEvent.objectId >= 0) state.diskMoveAudio.newlyCreatedSources.insert(superEvent.objectId);
                    advanceRuntimeDiskMoveAudio(state, runtimeWorldAudioClockNow(state));
                } else {
                    emitRuntimeSound(state, "sound:" + soundIntent, soundPosition, true);
                }
            }
        }
        if (localOwner
            && !evaIntent.empty()
            && (soundIntent.empty()
                || toLowerAsciiLocal(soundIntent)
                    != toLowerAsciiLocal(evaIntent))) {
            emitRuntimeSound(state, "sound:" + evaIntent);
        }
        if (state.visionEnabled
            && (superEvent.kind == "SpyPlaneRevealArea"
                || superEvent.kind == "PsychicRevealArea")
            && state.vision.revealArea(
                superEvent.owner,
                superEvent.targetCell.x,
                superEvent.targetCell.y,
                std::max(1, superEvent.value)
            )) {
            state.radarTerrainKey.clear();
            state.projectedObjectsCacheValid = false;
            state.repaintRequested = true;
        }
        if (superEvent.kind == "SuperWeaponCommandRejected") {
            state.interactionStatus =
                "superweapon target rejected: " + superEvent.reason;
            if (!state.runtimeSuppressPlayerAudioFeedback) {
                emitRuntimeSound(state, "blocked");
            }
        }
    };
}

bool runtimeIsForceShieldSuperWeaponRule(const SuperWeaponRule* rule) {
    if (rule == nullptr) {
        return false;
    }
    const std::string identity = toLowerAsciiLocal(
        trimAsciiLocal(rule->type) + " " + trimAsciiLocal(rule->action)
    );
    return identity.find("forceshield") != std::string::npos
        || identity.find("force shield") != std::string::npos;
}

bool advanceRuntimeSuperWeapons(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeSuperWeapons", state.tick);
    if (!state.rulesLoaded || state.rules.getSuperWeaponTypes().empty()) {
        return false;
    }
    ensureRuntimeSuperWeaponsInitialized(state);
    if (!state.superWeaponsInitialized) {
        return false;
    }
    const std::vector<std::array<int, 4>> parasiteHostPoses =
        runtimeParasiteLinkedHostPoses(state);
    const std::uint64_t beforeHash = state.superWeapons.stateHash();
    SuperWeaponWorldContext context = runtimeSuperWeaponContext(state);
    context.emit = runtimeSuperWeaponEventSink(state);
    state.superWeapons.tick(context);
    bool changed = beforeHash != state.superWeapons.stateHash();

    // Defensive super-weapons react to targeted hostile launches. Keep that
    // decision on the same deterministic command path as ordinary AI fire,
    // but update each computer player's base center first so the authored
    // distance gate is evaluated against current world state.
    const bool computerAiEnabled =
        runtimeComputerAiEnabledForSession(state);
    if (computerAiEnabled) {
        ensureRuntimeComputerAiStates(state);
    }
    std::vector<std::string> computerOwners;
    computerOwners.reserve(state.computerAiByOwner.size());
    for (const auto& [ownerKey, ai] : state.computerAiByOwner) {
        (void)ai;
        computerOwners.push_back(ownerKey);
    }
    std::sort(computerOwners.begin(), computerOwners.end());
    const SpecialMechanismRules& mechanismRules =
        state.rules.getSpecialMechanismRules();
    for (const std::string& ownerKey : computerOwners) {
        if (!computerAiEnabled) {
            break;
        }
        auto found = state.computerAiByOwner.find(ownerKey);
        if (found == state.computerAiByOwner.end()
            || !found->second.initialized
            || trimAsciiLocal(found->second.owner).empty()) {
            continue;
        }
        RuntimeComputerAiState& ai = found->second;
        updateRuntimeComputerAiBaseCenter(state, ai);
        const int probabilityIndex =
            runtimeAiSuperDefenseProbabilityIndex(ai.difficulty);
        std::string reason;
        if (state.superWeapons.fireAiDefense(
                ai.owner,
                {ai.baseCenter.x, ai.baseCenter.y},
                mechanismRules.aiSuperDefense.probabilityPercent[
                    static_cast<std::size_t>(probabilityIndex)
                ],
                mechanismRules.aiSuperDefense.alertFrames,
                mechanismRules.aiSuperDefense.distanceCells,
                context,
                &reason
            )) {
            changed = true;
        }
    }

    // The runtime does not have a separate computer-player command queue
    // yet.  Fire ready enemy super weapons at a fixed cadence through the
    // same validated command path used by the player, so AI effects remain
    // deterministic and replay-visible instead of bypassing the system.
    if (computerAiEnabled
        && state.tick > 0
        && (state.tick % std::max(1, context.fixedTickHz)) == 0) {
        std::vector<std::pair<std::string, std::string>> aiWeapons;
        for (const SuperWeaponSidebarSnapshot& snapshot :
                state.superWeapons.sidebarSnapshots()) {
            const SuperWeaponRule* rule = state.rules.findSuperWeaponType(
                snapshot.superWeaponId
            );
            if (snapshot.state == SuperWeaponRuntimeState::Ready
                && !runtimeIsForceShieldSuperWeaponRule(rule)
                && !runtimeComputerTacticReservesSuperWeapon(state, snapshot.owner, rule)
                && (state.campaignMode || state.computerAiByOwner.contains(toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))))
                && toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))
                    != toLowerAsciiLocal(trimAsciiLocal(state.economy.owner))) {
                aiWeapons.emplace_back(snapshot.owner, snapshot.superWeaponId);
            }
        }
        for (const auto& [owner, superWeaponId] : aiWeapons) {
            std::string reason;
            if (state.superWeapons.fireAi(owner, superWeaponId, context, &reason)) {
                changed = true;
            }
        }
    }
    changed = notifyRuntimeParasiteHostsRelocated(
        state,
        parasiteHostPoses
    ) || changed;
    changed = drainRuntimeObjectExitQueue(state) || changed;
    changed = flushRuntimeSuperWeaponHostEvents(state) || changed;
    return changed || !state.superWeapons.renderSnapshots().empty();
}

void applyRuntimeSuperWeaponLightingLifecycle(
    RuntimeProductionHostState& state,
    const SuperWeaponActiveLifecycleEvent& event) {
    if (event.activeToken <= 0) {
        return;
    }
    const std::string typeKey = toLowerAsciiLocal(event.superWeaponType);
    RuntimeLightingMode mode = RuntimeLightingMode::Base;
    if (typeKey.find("lightning") != std::string::npos) {
        mode = RuntimeLightingMode::Ion;
    } else if (typeKey.find("dominator") != std::string::npos) {
        mode = RuntimeLightingMode::Dominator;
    } else {
        return;
    }

    ensureRuntimeLightingInitialized(state);
    const std::uint64_t token = static_cast<std::uint64_t>(event.activeToken);
    if (event.phase == SuperWeaponActiveLifecyclePhase::Started) {
        state.activeLightingTokens[token] = {
            token,
            mode,
            event.startedTick
        };
    } else {
        state.activeLightingTokens.erase(token);
    }

    if (state.activeLightingTokens.empty()) {
        (void)state.lighting.restoreBase(event.tick, true);
    } else {
        const auto selected = std::max_element(
            state.activeLightingTokens.begin(),
            state.activeLightingTokens.end(),
            [](const auto& left, const auto& right) {
                return std::tie(left.second.startedTick, left.second.token)
                    < std::tie(
                        right.second.startedTick,
                        right.second.token
                    );
            }
        );
        if (selected->second.mode == RuntimeLightingMode::Ion) {
            (void)state.lighting.activateIon(event.tick);
        } else {
            (void)state.lighting.activateDominator(event.tick);
        }
    }
    state.repaintRequested = true;
}

std::map<std::uint64_t, RuntimeActiveLightingToken>
runtimeActiveLightingTokensForSuperWeapons(const RuntimeGameSessionState& state) {
    std::map<std::uint64_t, RuntimeActiveLightingToken> activeTokens;
    if (state.superWeaponsInitialized) {
        for (const SuperWeaponActiveLifecycleSnapshot& snapshot :
                state.superWeapons.activeLifecycleSnapshots()) {
            const std::string typeKey = toLowerAsciiLocal(
                snapshot.superWeaponType
            );
            RuntimeLightingMode mode = RuntimeLightingMode::Base;
            if (typeKey.find("lightning") != std::string::npos) {
                mode = RuntimeLightingMode::Ion;
            } else if (typeKey.find("dominator") != std::string::npos) {
                mode = RuntimeLightingMode::Dominator;
            } else {
                continue;
            }
            activeTokens.emplace(
                snapshot.activeToken,
                RuntimeActiveLightingToken{
                    static_cast<std::uint64_t>(snapshot.activeToken),
                    mode,
                    snapshot.startedTick
                }
            );
        }
    }
    return activeTokens;
}

bool syncRuntimeLightingSuperWeaponProfiles(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("syncRuntimeLightingSuperWeaponProfiles", state.tick);
    ensureRuntimeLightingInitialized(state);
    std::map<std::uint64_t, RuntimeActiveLightingToken> activeTokens =
        runtimeActiveLightingTokensForSuperWeapons(state);
    if (activeTokens == state.activeLightingTokens) {
        return false;
    }

    state.activeLightingTokens = std::move(activeTokens);
    if (state.activeLightingTokens.empty()) {
        (void)state.lighting.restoreBase(state.tick, true);
    } else {
        const auto selected = std::max_element(
            state.activeLightingTokens.begin(),
            state.activeLightingTokens.end(),
            [](const auto& left, const auto& right) {
                return std::tie(left.second.startedTick, left.second.token)
                    < std::tie(
                        right.second.startedTick,
                        right.second.token
                    );
            }
        );
        if (selected->second.mode == RuntimeLightingMode::Ion) {
            (void)state.lighting.activateIon(state.tick);
        } else {
            (void)state.lighting.activateDominator(state.tick);
        }
    }
    state.repaintRequested = true;
    return true;
}

bool runtimeObjectHasBombDisarmWeapon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    std::vector<std::string> weaponIds;
    const auto append = [&weaponIds](const std::string& weaponId) {
        if (!trimAsciiLocal(weaponId).empty()) {
            weaponIds.push_back(weaponId);
        }
    };
    if (object.kind == WorldObjectKind::Structure) {
        const BuildingType* building = findRuntimeBuildingTypeById(
            state.rules,
            object.type
        );
        if (building == nullptr) {
            return false;
        }
        append(building->primaryWeapon);
        append(building->secondaryWeapon);
        append(building->deployWeapon);
        for (const std::string& weaponId : building->indexedWeapons) {
            append(weaponId);
        }
        if (object.veterancy >= 2) {
            append(building->elitePrimaryWeapon);
            append(building->eliteSecondaryWeapon);
            append(building->eliteDeployWeapon);
            for (const std::string& weaponId :
                    building->eliteIndexedWeapons) {
                append(weaponId);
            }
        }
    } else {
        const UnitType* unit = findRuntimeUnitType(state.rules, object);
        if (unit == nullptr) {
            return false;
        }
        append(unit->primaryWeapon);
        append(unit->secondaryWeapon);
        append(unit->deployWeapon);
        append(unit->occupyWeapon);
        for (const std::string& weaponId : unit->indexedWeapons) {
            append(weaponId);
        }
        if (object.veterancy >= 2) {
            append(unit->elitePrimaryWeapon);
            append(unit->eliteSecondaryWeapon);
            append(unit->eliteDeployWeapon);
            append(unit->eliteOccupyWeapon);
            for (const std::string& weaponId : unit->eliteIndexedWeapons) {
                append(weaponId);
            }
        }
    }

    std::unordered_set<std::string> visited;
    for (const std::string& weaponId : weaponIds) {
        const std::string key = toLowerAsciiLocal(trimAsciiLocal(weaponId));
        if (key.empty() || !visited.insert(key).second) {
            continue;
        }
        const WeaponType* weapon = findRuntimeWeaponType(
            state.rules,
            weaponId
        );
        const WarheadType* warhead = weapon == nullptr
            ? nullptr
            : findRuntimeWarheadType(state.rules, weapon->warhead);
        if (weapon != nullptr && !weapon->neverUse
            && warhead != nullptr && warhead->bombDisarm) {
            return true;
        }
    }
    return false;
}

std::optional<SpecialMechanismRenderSnapshot> runtimeSuperWeaponBuildingSnapshot(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
        if (!object.alive || object.kind != WorldObjectKind::Structure) return std::nullopt;
        const auto* building = state.rules.findBuildingType(object.type);
        if (!building || building->superWeapon.empty()) return std::nullopt;
        const auto* art = state.art.findArtType(building->image.empty() ? object.type : building->image);
        if (!art || std::none_of(art->auxiliaryAnimations.begin(), art->auxiliaryAnimations.end(),
                [](const auto& layer) { return layer.key == "SuperAnim"; })) return std::nullopt;
        const auto* weapon = state.superWeapons.findSidebarSnapshot(object.owner, building->superWeapon);
        SpecialMechanismRenderSnapshot pose;
        pose.kind = "superWeaponBuilding";
        pose.objectId = object.instanceId;
        pose.owner = object.owner;
        pose.resourceId = "SuperAnim";
        if (weapon) {
            const auto phase = weapon->state;
            const int chargedTicks = std::max(0, static_cast<int>(std::lround(
                building->chargedAnimTime * 60.0 * std::max(1, state.config.fixedTickHz))));
            const int launchTick = state.superWeapons.activeLaunchTick(object.owner, building->superWeapon);
            if (launchTick >= 0) {
                pose.resourceId = "SuperAnimFour";
                pose.progress = std::max(0, state.tick - launchTick);
            } else if (phase == SuperWeaponRuntimeState::Ready || phase == SuperWeaponRuntimeState::SelectingTarget
                    || phase == SuperWeaponRuntimeState::AwaitingSecondTarget) {
                pose.resourceId = "SuperAnimThree";
            } else if ((phase == SuperWeaponRuntimeState::Charging || phase == SuperWeaponRuntimeState::Suspended)
                    && weapon->remainingTicks <= chargedTicks) {
                pose.resourceId = "SuperAnimTwo";
                // RemainingTicks stops while power is down, freezing this
                // transition at its actual frame and surviving a save/load.
                pose.progress = chargedTicks - weapon->remainingTicks;
            }
        }
        if (pose.resourceId == "SuperAnim" || pose.resourceId == "SuperAnimThree") {
            // Idle/ready art is often an infinite native SHP loop, not a
            // single frame. Use the building's saved powered animation clock.
            const auto layer = std::find_if(art->auxiliaryAnimations.begin(), art->auxiliaryAnimations.end(),
                [&](const auto& entry) { return entry.key == pose.resourceId; });
            const bool poweredAnimation = layer != art->auxiliaryAnimations.end()
                && (layer->powered || layer->poweredEffect);
            const int sourceTick = poweredAnimation && !object.poweredOperational
                && object.powerAnimationPausedAtTick >= 0 ? object.powerAnimationPausedAtTick : state.tick;
            pose.progress = std::max(0, sourceTick
                - (poweredAnimation ? std::max(0, object.powerAnimationPausedTicks) : 0));
        }
        return pose;
}

void appendRuntimeSuperWeaponBuildingSnapshots(RuntimeGameSessionState& state) {
    for (const auto& object : state.world.getObjects()) {
        if(auto pose=runtimeSuperWeaponBuildingSnapshot(state,object))
            state.specialMechanismRenderSnapshots.push_back(std::move(*pose));
    }
}

RuntimeNuclearFlightPose runtimeSuperWeaponNuclearFlightPose(
    const RuntimeGameSessionState& state, const SuperWeaponRenderSnapshot& missile) {
    return runtimeNuclearFlightPose(missile.progress, missile.requiredProgress,
        missile.sourceObjectId >= 0, missile.cell2.x, missile.cell2.y, missile.sourceLevel,
        missile.cell.x, missile.cell.y,
        terrainLevelAtRuntimeCell(state, missile.cell.x, missile.cell.y),
        state.rules.findSuperWeaponType(missile.superWeaponId));
}

bool runtimeMissionAllowsDeployedWeapon(const WorldObjectInstance& object) {
    const std::string mission = toLowerAsciiLocal(object.mission);
    return mission == "deployed"
        || mission == "deployedidle"
        || mission == "deployedfire"
        || (object.deployed
            && mission != "landingtodeploy"
            && mission != "deploying"
            && mission != "undeploying"
            && mission != "takingoffafterundeploy");
}

bool runtimeObjectCanUseDeployedWeapon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (runtimeMissionAllowsDeployedWeapon(object)) {
        return true;
    }

    const auto behaviorIt = state.behaviors.find(object.instanceId);
    return behaviorIt != state.behaviors.end()
        && behaviorIt->second.returnToDeployed
        && !runtimeMissionIsDeploying(object)
        && !runtimeMissionIsUndeploying(object);
}

std::string runtimeWeaponSlotName(RuntimeWeaponSlot slot) {
    switch (slot) {
    case RuntimeWeaponSlot::Primary:
        return "primary";
    case RuntimeWeaponSlot::Secondary:
        return "secondary";
    case RuntimeWeaponSlot::ElitePrimary:
        return "elitePrimary";
    case RuntimeWeaponSlot::EliteSecondary:
        return "eliteSecondary";
    case RuntimeWeaponSlot::Deploy:
        return "deploy";
    case RuntimeWeaponSlot::EliteDeploy:
        return "eliteDeploy";
    case RuntimeWeaponSlot::Occupy:
        return "occupy";
    case RuntimeWeaponSlot::EliteOccupy:
        return "eliteOccupy";
    case RuntimeWeaponSlot::OpenTransport:
        return "openTransport";
    case RuntimeWeaponSlot::Indexed:
        return "indexed";
    case RuntimeWeaponSlot::EliteIndexed:
        return "eliteIndexed";
    case RuntimeWeaponSlot::None:
        break;
    }
    return "none";
}

std::string runtimeDeployedWeaponId(const UnitType& unit) {
    if (!unit.deployWeapon.empty()) {
        return unit.deployWeapon;
    }
    if (!unit.eliteDeployWeapon.empty()) {
        return unit.eliteDeployWeapon;
    }
    if (!unit.secondaryWeapon.empty()) {
        return unit.secondaryWeapon;
    }
    return unit.eliteSecondaryWeapon;
}

RuntimeWeaponSlot runtimeDeployedWeaponSlot(const UnitType& unit) {
    if (!unit.deployWeapon.empty()) {
        return RuntimeWeaponSlot::Deploy;
    }
    if (!unit.eliteDeployWeapon.empty()) {
        return RuntimeWeaponSlot::EliteDeploy;
    }
    if (!unit.secondaryWeapon.empty()) {
        return RuntimeWeaponSlot::Secondary;
    }
    if (!unit.eliteSecondaryWeapon.empty()) {
        return RuntimeWeaponSlot::EliteSecondary;
    }
    return RuntimeWeaponSlot::None;
}

RuntimeTransportProfile runtimeTransportProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    return runtimeCachedCapabilityProfileForObject(state, object).transport;
}

int runtimeTransportPassengerSizeLimit(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& transport) {
    if (const auto* building = runtimeInfantryAbsorberType(state, transport)) {
        return building->sizeLimit;
    }
    const UnitType* unit = findRuntimeUnitType(state.rules, transport);
    return unit == nullptr ? 0 : unit->sizeLimit;
}

bool runtimeWeaponHasAttackRange(const WeaponType& weapon) {
    return weapon.range > 0.0 || runtimeSpawnerHasInfiniteRange(weapon);
}

void addRuntimeWeaponCandidate(
    std::vector<RuntimeWeaponCandidate>& candidates,
    RuntimeWeaponSlot slot,
    const std::string& weaponId) {
    if (weaponId.empty()) {
        return;
    }
    for (const RuntimeWeaponCandidate& candidate : candidates) {
        if (candidate.first == slot && candidate.second == weaponId) {
            return;
        }
    }
    candidates.emplace_back(slot, weaponId);
}

bool runtimeObjectIsOpenTransportWeaponContext(
    const WorldObjectInstance& object) {
    const std::string mission = toLowerAsciiLocal(object.mission);
    return mission == "loading"
        || mission == "load"
        || mission == "unloading"
        || mission == "unload";
}

std::optional<RuntimeWeaponCandidate> runtimeOpenTransportWeaponCandidate(
    const UnitType& unit) {
    if (unit.openTransportWeapon < 0) {
        return std::nullopt;
    }

    if (unit.openTransportWeapon == 0 && !unit.primaryWeapon.empty()) {
        return RuntimeWeaponCandidate{
            RuntimeWeaponSlot::OpenTransport,
            unit.primaryWeapon
        };
    }
    if (unit.openTransportWeapon == 1 && !unit.secondaryWeapon.empty()) {
        return RuntimeWeaponCandidate{
            RuntimeWeaponSlot::OpenTransport,
            unit.secondaryWeapon
        };
    }
    if (unit.openTransportWeapon == 2 && !unit.elitePrimaryWeapon.empty()) {
        return RuntimeWeaponCandidate{
            RuntimeWeaponSlot::OpenTransport,
            unit.elitePrimaryWeapon
        };
    }
    if (unit.openTransportWeapon == 3 && !unit.eliteSecondaryWeapon.empty()) {
        return RuntimeWeaponCandidate{
            RuntimeWeaponSlot::OpenTransport,
            unit.eliteSecondaryWeapon
        };
    }

    return std::nullopt;
}

bool runtimeWeaponSlotIsPrimary(RuntimeWeaponSlot slot) {
    // Slots without an authored primary/secondary family (deploy, occupy,
    // transport and indexed weapons) retain the engine's normal first-weapon
    // behavior.  The explicitly secondary slots below are the only ones that
    // may satisfy rules that demand a secondary naval/land weapon.
    return slot != RuntimeWeaponSlot::None
        && slot != RuntimeWeaponSlot::Secondary
        && slot != RuntimeWeaponSlot::EliteSecondary;
}

bool runtimeWeaponSlotIsSecondary(RuntimeWeaponSlot slot) {
    return slot == RuntimeWeaponSlot::Secondary
        || slot == RuntimeWeaponSlot::EliteSecondary;
}

bool runtimeWeaponSlotCanTargetDomain(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    RuntimeWeaponSlot slot,
    const WorldObjectInstance& target,
    bool forceFire) {
    const RuntimeTargetDomain domain =
        runtimeTargetDomainForObject(state, target);
    if (domain == RuntimeTargetDomain::Air) {
        return true;
    }

    const RuntimeCachedCapabilityProfile& attackerProfile =
        runtimeCachedCapabilityProfileForObject(state, attacker);
    const bool primary = runtimeWeaponSlotIsPrimary(slot);
    const bool secondary = runtimeWeaponSlotIsSecondary(slot);
    if (domain == RuntimeTargetDomain::Land) {
        switch (attackerProfile.landTargeting) {
        case 0: // LAND_OKAY
            return true;
        case 1: // LAND_NOT_OKAY
            return false;
        case 2: // LAND_SECONDARY
            return secondary;
        default:
            return false;
        }
    }

    const RuntimeCachedCapabilityProfile& targetProfile =
        runtimeCachedCapabilityProfileForObject(state, target);
    switch (attackerProfile.navalTargeting) {
    case 0: // UNDERWATER_NEVER
        return forceFire || !target.submerged;
    case 1: // UNDERWATER_SECONDARY
        if (targetProfile.underwater && !attackerProfile.spawned) {
            return secondary;
        }
        return primary;
    case 2: // UNDERWATER_ONLY
        return targetProfile.underwater;
    case 3: // ORGANIC_SECONDARY
        return targetProfile.organic ? secondary : primary;
    case 4: { // SEAL_SPECIAL
        const std::string speedType =
            toLowerAsciiLocal(targetProfile.speedType);
        const bool navalNonOrganic = targetProfile.naval
            && !targetProfile.organic
            && (target.kind == WorldObjectKind::Structure
                || speedType.find("float") != std::string::npos);
        return navalNonOrganic ? secondary : primary;
    }
    case 5: // NAVAL_ALL
        return true;
    case 6: // NAVAL_NONE
        return false;
    case 7: // NAVAL_PRIMARY (stock YR Boomer contract)
        return primary;
    default:
        return false;
    }
}

void finalizeRuntimeWeaponProfile(
    RuntimeWeaponProfile& profile,
    const RuntimeGameSessionState& state,
    RuntimeWeaponSlot slot,
    const WeaponType& weapon,
    const std::string& reason) {
    profile.weapon = &weapon;
    profile.slot = slot;
    profile.hasWeaponSet = true;
    profile.weaponId = weapon.id;
    profile.projectileId = weapon.projectile;
    profile.warheadId = weapon.warhead;
    profile.projectile =
        findRuntimeProjectileType(state.rules, profile.projectileId);
    profile.warhead =
        findRuntimeWarheadType(state.rules, profile.warheadId);
    profile.selectionReason = reason;
}

const RuntimeAirstrikePlaneBinding* runtimeAirstrikeBindingForObject(
    const RuntimeGameSessionState& state,
    int objectId) {
    const RuntimeAirstrikePlaneBinding* match = nullptr;
    for (const auto& [planeToken, binding] :
            state.airstrikePlanesByToken) {
        if (planeToken <= 0
            || binding.planeToken != planeToken
            || binding.objectId != objectId) {
            continue;
        }
        if (match != nullptr) {
            return nullptr;
        }
        match = &binding;
    }
    return match;
}

bool runtimeObjectManagedByAirstrike(
    const RuntimeGameSessionState& state,
    int objectId) {
    return runtimeAirstrikeBindingForObject(state, objectId) != nullptr;
}

const RuntimeAirstrikeRootAuthority* runtimeAirstrikeRootAuthorityForPair(
    const RuntimeGameSessionState& state,
    int strikeId,
    int planeToken) {
    const RuntimeAirstrikeRootAuthority* match = nullptr;
    for (const auto& [rootActorId, authority] :
            state.airstrikeRootAuthoritiesByActor) {
        if (rootActorId <= 0
            || authority.rootProjectileActorId != rootActorId
            || authority.strikeId != strikeId
            || authority.planeToken != planeToken) {
            continue;
        }
        if (match != nullptr) {
            return nullptr;
        }
        match = &authority;
    }
    return match;
}

const RuntimeAirstrikePlaneState* runtimeAirstrikePlaneStateForToken(
    const RuntimeAirstrikeSystemState& airstrikes,
    int planeToken,
    const RuntimeAirstrikeStrikeState** strikeOutput) {
    const RuntimeAirstrikePlaneState* match = nullptr;
    const RuntimeAirstrikeStrikeState* matchStrike = nullptr;
    for (const RuntimeAirstrikeStrikeState& strike : airstrikes.strikes()) {
        for (const RuntimeAirstrikePlaneState& plane : strike.planes) {
            if (plane.planeToken != planeToken) {
                continue;
            }
            if (match != nullptr) {
                return nullptr;
            }
            match = &plane;
            matchStrike = &strike;
        }
    }
    if (strikeOutput != nullptr) {
        *strikeOutput = matchStrike;
    }
    return match;
}

bool runtimeWeaponDisguiseRequirementSatisfied(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const WeaponType& weapon) {
    if (!weapon.disguiseFireOnly) {
        return true;
    }
    return (state.spiesInitialized
            && state.spies.isDisguised(object.instanceId))
        || (state.specialMechanismsInitialized
            && state.specialMechanisms.isCamouflaged(object.instanceId));
}

RuntimeWeaponProfile runtimeWeaponProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const WorldObjectInstance* target,
    bool forceFire) {
    RuntimeWeaponProfile profile;
    if (!state.rulesLoaded
        || runtimeObjectDisabledByParasite(state, object.instanceId)
        || runtimeObjectManagedByAirstrike(state, object.instanceId)) {
        return profile;
    }

    RuntimeCapabilityCacheStats& stats = state.capabilityCache.stats;
    ++stats.weaponLookups;
    ++stats.lastTickWeaponLookups;

    const RuntimeCachedCapabilityProfile& cached =
        runtimeCachedCapabilityProfileForObject(state, object);
    if (cached.rulesResolved) {
        ++stats.weaponCandidateHits;
        ++stats.lastTickWeaponCandidateHits;
    } else {
        ++stats.weaponCandidateMisses;
        ++stats.lastTickWeaponCandidateMisses;
    }

    profile.deployed = runtimeObjectCanUseDeployedWeapon(state, object);
    profile.occupied = runtimeObjectIsOccupyingMission(object);
    profile.openTransport = runtimeObjectIsOpenTransportWeaponContext(object);
    profile.hasTurret = cached.hasTurret;
    profile.requiresDeployment = cached.deploy.requiresDeploymentToFire;
    if (cached.deploy.requiresDeploymentToFire && !profile.deployed) {
        return profile;
    }

    std::array<RuntimeWeaponSlot, 96> attemptedSlots{};
    std::array<const std::string*, 96> attemptedWeaponIds{};
    std::size_t attemptedCount = 0;
    const auto tryCandidate =
        [&](
            const RuntimeWeaponCandidate& candidate,
            const char* reason
        ) {
        if (candidate.second.empty()) {
            return false;
        }
        for (std::size_t index = 0; index < attemptedCount; ++index) {
            if (attemptedSlots[index] == candidate.first
                && attemptedWeaponIds[index] != nullptr
                && *attemptedWeaponIds[index] == candidate.second) {
                return false;
            }
        }
        if (attemptedCount < attemptedWeaponIds.size()) {
            attemptedSlots[attemptedCount] = candidate.first;
            attemptedWeaponIds[attemptedCount] = &candidate.second;
            ++attemptedCount;
        }
        const WeaponType* weapon =
            findRuntimeWeaponType(state.rules, candidate.second);
        if (weapon == nullptr
            || weapon->neverUse
            || !runtimeWeaponDisguiseRequirementSatisfied(
                state,
                object,
                *weapon
            )) {
            return false;
        }
        if (target != nullptr) {
            if (weapon->drainWeapon && runtimeOwnersAreAllied(state, object.owner, target->owner)) return false;
            const bool areaFireSelf = weapon->areaFire
                && target->instanceId == object.instanceId;
            const WarheadType* targetWarhead = findRuntimeWarheadType(
                state.rules,
                weapon->warhead
            );
            // Control relationships cannot target allies, including the current owner.
            // Force fire and berserk targeting do not waive this mechanism rule.
            if (targetWarhead != nullptr && targetWarhead->mindControl
                && runtimeOwnersAreAllied(state, object.owner, target->owner)) {
                return false;
            }
            const bool hiddenIvanBomb = targetWarhead != nullptr
                && targetWarhead->bombDisarm
                && state.specialMechanismsInitialized
                && state.specialMechanisms.timedBombs().contains(
                    target->instanceId
                )
                && !runtimeTimedBombDetectedForOwner(
                    state,
                    target->instanceId,
                    object.owner
                );
            if (hiddenIvanBomb) {
                return false;
            }
            // The source keeps its existing locomotor link after the victim
            // becomes an air target. Other weapons use the normal air domain.
            const bool ownsTractorLink = targetWarhead != nullptr && targetWarhead->isLocomotor
                && std::any_of(state.magBeams.bindings().begin(), state.magBeams.bindings().end(),
                    [&](const RuntimeMagBeamBindingState& binding) {
                        return binding.sourceObjectId == object.instanceId
                            && binding.targetObjectId == target->instanceId
                            && binding.phase != RuntimeMagBeamPhase::Falling
                            && toLowerAsciiLocal(binding.weaponId) == toLowerAsciiLocal(weapon->id);
                    });
            if ((!areaFireSelf && !ownsTractorLink && !runtimeWeaponSlotCanTargetDomain(
                    state,
                    object,
                    candidate.first,
                    *target,
                    forceFire
                ))
                || (!areaFireSelf && !ownsTractorLink && !runtimeWeaponCanTargetObjectFromSource(
                    state,
                    object,
                    *weapon,
                    *target
                ))) {
                return false;
            }
        }
        finalizeRuntimeWeaponProfile(
            profile,
            state,
            candidate.first,
            *weapon,
            reason
        );
        return true;
    };

    const char* selectionReason =
        target == nullptr ? "firstAvailable" : "targetProfile";
    const bool elite = object.veterancy >= 2;
    const auto candidateUsesEliteRank = [](RuntimeWeaponSlot slot) {
        return slot == RuntimeWeaponSlot::ElitePrimary
            || slot == RuntimeWeaponSlot::EliteSecondary
            || slot == RuntimeWeaponSlot::EliteDeploy
            || slot == RuntimeWeaponSlot::EliteOccupy
            || slot == RuntimeWeaponSlot::EliteIndexed;
    };
    const auto tryRankedCandidates = [
        &tryCandidate,
        &candidateUsesEliteRank,
        elite,
        selectionReason
    ](const std::vector<RuntimeWeaponCandidate>& candidates) {
        if (elite) {
            for (const RuntimeWeaponCandidate& candidate : candidates) {
                if (candidateUsesEliteRank(candidate.first)
                    && tryCandidate(candidate, selectionReason)) {
                    return true;
                }
            }
        }
        for (const RuntimeWeaponCandidate& candidate : candidates) {
            if (!candidateUsesEliteRank(candidate.first)
                && tryCandidate(candidate, selectionReason)) {
                return true;
            }
        }
        return false;
    };
    if (profile.deployed) {
        if (tryRankedCandidates(cached.deployWeaponCandidates)) {
            return profile;
        }
    }
    if (profile.occupied) {
        if (tryRankedCandidates(cached.occupyWeaponCandidates)) {
            return profile;
        }
    }
    if (target != nullptr) {
        const bool targetHasTimedBomb = state.specialMechanismsInitialized
            && state.specialMechanisms.timedBombs().find(target->instanceId)
                != state.specialMechanisms.timedBombs().end();
        const BuildingType* targetBuilding = target->kind
                == WorldObjectKind::Structure
            ? findRuntimeBuildingTypeById(state.rules, target->type)
            : nullptr;
        const UnitType* targetUnit = target->kind
                == WorldObjectKind::Structure
            ? nullptr
            : findRuntimeUnitType(state.rules, *target);
        const bool targetAcceptsIvan =
            targetBuilding != nullptr
            || (targetUnit != nullptr && targetUnit->bombable);
        const auto tryTimedBombRelationship = [&](bool eliteRank) {
            for (const RuntimeWeaponCandidate& candidate :
                    cached.baseWeaponCandidates) {
                if (candidateUsesEliteRank(candidate.first) != eliteRank) {
                    continue;
                }
                const WeaponType* candidateWeapon = findRuntimeWeaponType(
                    state.rules,
                    candidate.second
                );
                const WarheadType* candidateWarhead = candidateWeapon == nullptr
                    ? nullptr
                    : findRuntimeWarheadType(
                        state.rules,
                        candidateWeapon->warhead
                    );
                const bool bombDisarm = candidateWarhead != nullptr
                    && candidateWarhead->bombDisarm
                    && targetHasTimedBomb
                    && runtimeTimedBombDetectedForOwner(
                        state,
                        target->instanceId,
                        object.owner
                    );
                const bool ivanBomb = candidateWarhead != nullptr
                    && candidateWarhead->ivanBomb
                    && targetAcceptsIvan
                    && !targetHasTimedBomb;
                if ((bombDisarm || ivanBomb)
                    && tryCandidate(
                        candidate,
                        bombDisarm ? "bombDisarm" : "ivanBomb"
                    )) {
                    return true;
                }
            }
            return false;
        };
        if (elite && tryTimedBombRelationship(true)) {
            return profile;
        }
        if (tryTimedBombRelationship(false)) {
            return profile;
        }
    }
    if (profile.openTransport
        && cached.openTransportWeapon.has_value()
        && tryCandidate(*cached.openTransportWeapon, selectionReason)) {
        return profile;
    }
    // A Tesla Trooper (or any MOD weapon carrying ElectricAssault) targets
    // an allied Overpowerable building to establish a charge relationship,
    // not to deal ordinary damage.  Resolve that authored warhead before the
    // normal primary/secondary order so a primary electric bolt cannot steal
    // the recharge command.
    if (target != nullptr
        && target->kind == WorldObjectKind::Structure
        && runtimeOwnersAreAllied(state, object.owner, target->owner)) {
        const BuildingType* targetBuilding =
            findRuntimeBuildingTypeById(state.rules, target->type);
        if (targetBuilding != nullptr && targetBuilding->overpowerable) {
            for (const RuntimeWeaponCandidate& candidate :
                    cached.baseWeaponCandidates) {
                const WeaponType* candidateWeapon =
                    findRuntimeWeaponType(state.rules, candidate.second);
                const WarheadType* candidateWarhead = candidateWeapon == nullptr
                    ? nullptr
                    : findRuntimeWarheadType(
                        state.rules,
                        candidateWeapon->warhead
                    );
                if (candidateWarhead != nullptr
                    && candidateWarhead->electricAssault
                    && tryCandidate(candidate, "electricAssaultCharge")) {
                    return profile;
                }
            }
        }
    }
    // An Overpowerable defense uses its authored secondary weapon while a
    // live charge exists.  This is deliberately keyed by the generic state
    // relationship and not by TESLA/CARRIER/YURI ids.
    if (state.specialMechanismsInitialized
        && state.specialMechanisms.isElectricCharged(
            object.instanceId,
            state.tick
        )) {
        for (const RuntimeWeaponCandidate& candidate :
                cached.baseWeaponCandidates) {
            if (candidate.first != RuntimeWeaponSlot::Secondary
                && candidate.first != RuntimeWeaponSlot::EliteSecondary) {
                continue;
            }
            if (tryCandidate(candidate, "electricCharged")) {
                return profile;
            }
        }
    }
    const std::vector<RuntimeWeaponCandidate>& indexedCandidates =
        elite && !cached.eliteIndexedWeaponCandidates.empty()
            ? cached.eliteIndexedWeaponCandidates
            : cached.indexedWeaponCandidates;
    if (object.activeGunnerWeaponIndex >= 0
        && static_cast<std::size_t>(object.activeGunnerWeaponIndex)
            < indexedCandidates.size()) {
        const std::size_t weaponIndex =
            static_cast<std::size_t>(object.activeGunnerWeaponIndex);
        if (tryCandidate(
                indexedCandidates[weaponIndex],
                elite ? "eliteGunnerMode" : "gunnerMode"
            )) {
            profile.indexedWeaponIndex = static_cast<int>(weaponIndex);
        }
        // An occupied gunner vehicle must not silently fall back to its empty
        // weapon when the active mode cannot affect the selected target.
        return profile;
    }
    if (cached.gattling && !indexedCandidates.empty()) {
        const int timer =
            [&state, &object]() {
                const auto found =
                    state.gattlingWeaponTimers.find(object.instanceId);
                return found == state.gattlingWeaponTimers.end()
                    ? 0
                    : std::max(0, found->second);
            }();
        int stage = 0;
        const int stageCount = std::max(1, cached.weaponStages);
        const std::vector<int>& stageThresholds =
            runtimeGattlingStageThresholds(cached, elite);
        for (int nextStage = 1;
            nextStage < stageCount
                && static_cast<std::size_t>(nextStage - 1)
                    < stageThresholds.size();
            ++nextStage) {
            if (timer < stageThresholds[
                    static_cast<std::size_t>(nextStage - 1)]) {
                break;
            }
            stage = nextStage;
        }
        const int domainOffset =
            target != nullptr && runtimeObjectIsAirTarget(state, *target)
                ? 1
                : 0;
        const std::size_t weaponIndex =
            static_cast<std::size_t>((stage * 2) + domainOffset);
        if (weaponIndex < indexedCandidates.size()
            && tryCandidate(
                indexedCandidates[weaponIndex],
                elite ? "eliteGattlingStage" : "gattlingStage"
            )) {
            profile.indexedWeaponIndex = static_cast<int>(weaponIndex);
            return profile;
        }
        return profile;
    }
    if (indexedCandidates.size() == 1U
        && tryCandidate(
            indexedCandidates.front(),
            elite ? "eliteSingleIndexedWeapon" : "singleIndexedWeapon"
        )) {
        profile.indexedWeaponIndex = 0;
        return profile;
    }
    // A bridge repair hut uses the authored demolition relationship even
    // though the hut itself is immune to ordinary weapon damage.
    if (target != nullptr && target->kind == WorldObjectKind::Structure) {
        const BuildingType* targetBuilding =
            findRuntimeBuildingTypeById(state.rules, target->type);
        if (targetBuilding != nullptr && targetBuilding->bridgeRepairHut
            && targetBuilding->canC4 && targetBuilding->bombable) {
            for (int pass = 0; pass < (elite ? 2 : 1); ++pass) {
                for (const auto& candidate : cached.baseWeaponCandidates) {
                    if (candidateUsesEliteRank(candidate.first) != (elite && pass == 0)) continue;
                    const auto* weapon = findRuntimeWeaponType(state.rules, candidate.second);
                    if (weapon != nullptr && weapon->sabotageCursor
                        && tryCandidate(candidate, "bridgeHutSabotage")) return profile;
                }
            }
        }
    }
    // Drainable buildings prefer the authored drain slot, including the base
    // secondary when an elite disc only overrides its ordinary laser.
    if (target != nullptr && target->kind == WorldObjectKind::Structure) {
        for (int pass = 0; pass < 2; ++pass) {
            for (const RuntimeWeaponCandidate& candidate : cached.baseWeaponCandidates) {
                const bool eliteCandidate = candidate.first == RuntimeWeaponSlot::ElitePrimary
                    || candidate.first == RuntimeWeaponSlot::EliteSecondary;
                if (eliteCandidate != (elite && pass == 0)) continue;
                const auto* candidateWeapon = findRuntimeWeaponType(state.rules, candidate.second);
                if (candidateWeapon && candidateWeapon->drainWeapon && tryCandidate(candidate, "diskDrain"))
                    return profile;
            }
            if (!elite) break;
        }
    }
    // Boris' Flare and compatible mod weapons use MigAttackCursor to mark a
    // building-targeted airstrike/designator weapon.  Prefer that authored
    // candidate before the ordinary primary weapon so combat and the
    // corresponding VoiceSecondaryWeaponAttack stay on the same slot.
    if (target != nullptr && target->kind == WorldObjectKind::Structure) {
        for (const RuntimeWeaponCandidate& candidate :
                cached.baseWeaponCandidates) {
            const bool eliteCandidate =
                candidate.first == RuntimeWeaponSlot::ElitePrimary
                || candidate.first == RuntimeWeaponSlot::EliteSecondary;
            if (eliteCandidate != elite) {
                continue;
            }
            const WeaponType* candidateWeapon =
                findRuntimeWeaponType(state.rules, candidate.second);
            if (candidateWeapon != nullptr
                && candidateWeapon->migAttackCursor
                && tryCandidate(candidate, "migAttackCursor")) {
                return profile;
            }
        }
        // Elite units without an elite designator retain the stock base
        // designator instead of falling back to their direct-fire weapon.
        if (elite) {
            for (const RuntimeWeaponCandidate& candidate :
                    cached.baseWeaponCandidates) {
                if (candidate.first != RuntimeWeaponSlot::Secondary) {
                    continue;
                }
                const WeaponType* candidateWeapon =
                    findRuntimeWeaponType(state.rules, candidate.second);
                if (candidateWeapon != nullptr
                    && candidateWeapon->migAttackCursor
                    && tryCandidate(candidate, "baseMigAttackCursor")) {
                    return profile;
                }
            }
        }
    }
    if (elite) {
        for (const RuntimeWeaponCandidate& candidate :
                cached.baseWeaponCandidates) {
            if ((candidate.first == RuntimeWeaponSlot::ElitePrimary
                    || candidate.first == RuntimeWeaponSlot::EliteSecondary)
                && tryCandidate(candidate, "eliteTargetProfile")) {
                return profile;
            }
        }
    }
    for (const RuntimeWeaponCandidate& candidate :
            cached.baseWeaponCandidates) {
        if (candidate.first == RuntimeWeaponSlot::ElitePrimary
            || candidate.first == RuntimeWeaponSlot::EliteSecondary) {
            continue;
        }
        if (tryCandidate(candidate, selectionReason)) {
            return profile;
        }
    }

    return profile;
}

RuntimeWeaponProfile runtimeWeaponProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    return runtimeWeaponProfileForObject(state, object, nullptr);
}

bool runtimePsychicCanAttackTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor,
    const WorldObjectInstance& candidate,
    bool hostTargetLegal,
    bool ordinaryRelationLegal) {
    const RuntimePsychicStatusRecord* psychicStatus =
        state.psychicStatuses.findByTarget(actor.instanceId);
    if (psychicStatus == nullptr || state.tick >= psychicStatus->expiresTick) {
        return hostTargetLegal && ordinaryRelationLegal;
    }

    RuntimePsychicTargetingQuery query;
    query.currentTick = state.tick;
    query.actor = runtimePsychicObjectSnapshot(state, actor);
    query.candidate = runtimePsychicObjectSnapshot(state, candidate);
    query.originalRelation = runtimePsychicRelationForObjects(
        state,
        actor,
        candidate
    );
    query.hostTargetLegal = hostTargetLegal;
    const RuntimePsychicTargetingResult result =
        state.psychicStatuses.queryTargeting(query);
    return result.berserkActive
        ? result.canAttack
        : hostTargetLegal && ordinaryRelationLegal;
}

bool runtimeGarrisonHostCanAttackTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& host,
    const WorldObjectInstance& target,
    bool forceFire) {
    if (!host.alive
        || host.kind != WorldObjectKind::Structure
        || host.instanceId == target.instanceId
        || host.garrisonOccupantIds.empty()
        || (!forceFire && runtimeOwnersAreAllied(state, host.owner, target.owner))) {
        return false;
    }
    const BuildingType* building = findRuntimeBuildingType(
        state.rules,
        host
    );
    if (building == nullptr || !building->canOccupyFire) {
        return false;
    }
    const double distance = runtimeCellDistanceToObjectFootprint(
        state,
        runtimeCellPointForObject(host),
        target
    );
    if (distance > state.garrisons.config().occupyWeaponRange) {
        return false;
    }
    for (int occupantId : host.garrisonOccupantIds) {
        const auto found = std::find_if(
            state.world.getObjects().begin(),
            state.world.getObjects().end(),
            [occupantId](const WorldObjectInstance& object) {
                return object.instanceId == occupantId;
            }
        );
        if (found == state.world.getObjects().end()
            || found->alive
            || found->health <= 0
            || found->garrisonHostObjectId != host.instanceId
            || found->owner != host.owner) {
            continue;
        }
        const RuntimeWeaponProfile profile =
            runtimeGarrisonWeaponProfileForOccupant(state, *found, &target, forceFire);
        if (profile.weapon != nullptr
            && (profile.weapon->damage > 0 || runtimeWeaponHasAttachEffect(state.rules, profile.weapon))
            && distance >= profile.weapon->minimumRange
            && runtimeWeaponHasLineOfSight(
                state,
                host,
                target,
                *profile.weapon
            )) {
            return true;
        }
    }
    return false;
}

bool runtimeObjectDisabledByParasite(
    const RuntimeGameSessionState& state,
    int objectId) {
    // MagBeam owns the target locomotor until landing/restore. Reuse the
    // existing disabled-object gate for every command and AI entry point.
    if (runtimeObjectSuspendedByMagBeam(state, objectId)) {
        return true;
    }
    if (!state.specialMechanismsInitialized) {
        return false;
    }
    if (state.specialMechanisms.isParasiteSourceLimboed(objectId)
        || state.specialMechanisms.isParasiteHostParalyzed(objectId)) {
        return true;
    }
    const auto stun = state.parasiteStunUntilTickByObject.find(objectId);
    return stun != state.parasiteStunUntilTickByObject.end()
        && state.tick < stun->second;
}

std::string runtimeDamageFeedbackVoiceField(const UnitType& unit) {
    if (!unit.ivan) return unit.voiceFeedback;
    // CrazyIvanFear reuses selection laughter ($icraseg/$icrasea).
    // Do not treat that stock cue as an automatic hurt voice. Keep custom
    // feedback entries and the normal select/move/attack/death routes.
    std::string voices;
    for (const auto& token : splitRuntimeTokenList(unit.voiceFeedback)) {
        if (toLowerAsciiLocal(trimAsciiLocal(token)) == "crazyivanfear") continue;
        if (!voices.empty()) voices += ',';
        voices += token;
    }
    return voices;
}

bool runtimeWeaponHasLineOfSight(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& target,
    const WeaponType& weapon) {
    if (weapon.limboLaunch) {
        return true;
    }
    const ProjectileType* projectile =
        findRuntimeProjectileType(state.rules, weapon.projectile);
    if (projectile == nullptr) {
        return false;
    }
    const WarheadType* warhead =
        findRuntimeWarheadType(state.rules, weapon.warhead);
    const bool checkWalls = projectile->subjectToWalls
        && (warhead == nullptr || !warhead->wall);
    const bool checkCliffs = projectile->subjectToCliffs
        && projectile->rot == 0;
    if (!checkWalls && !checkCliffs) {
        return true;
    }

    // World state is constant for this ray. Resolve potential walls once,
    // instead of rescanning every unit and building at each traversed cell.
    // Keep footprint, temporary off-ground state and height tests below so
    // multi-cell walls and lifted objects retain their existing semantics.
    std::vector<const WorldObjectInstance*> wallCandidates;
    if (checkWalls) {
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive
                || object.instanceId == source.instanceId
                || object.instanceId == target.instanceId
                || object.kind != WorldObjectKind::Structure) {
                continue;
            }
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, object.type);
            if (building != nullptr && building->wall) {
                wallCandidates.push_back(&object);
            }
        }
        if (wallCandidates.empty() && !checkCliffs) {
            return true;
        }
    }

    const RuntimeProjectileTargetPosition from =
        runtimeProjectileTargetPosition(state, source);
    const RuntimeProjectileTargetPosition to =
        runtimeProjectileTargetPosition(state, target);
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const int sampleCount = std::max(
        1,
        static_cast<int>(std::ceil(
            std::max(std::abs(dx), std::abs(dy)) * 4.0
        ))
    );
    RuntimeCellPoint previousCell = from.cell;
    bool previousCellWasCliff = false;
    for (int sample = 1; sample <= sampleCount; ++sample) {
        const double t = static_cast<double>(sample)
            / static_cast<double>(sampleCount);
        const double sampleZ = static_cast<double>(from.level)
            + static_cast<double>(to.level - from.level) * t;
        const RuntimeCellPoint cell{
            static_cast<int>(std::round(from.x + dx * t)),
            static_cast<int>(std::round(from.y + dy * t))
        };
        if (cell == previousCell) {
            continue;
        }
        previousCell = cell;

        if (checkWalls) {
            for (const WorldObjectInstance* wall : wallCandidates) {
                if (runtimeObjectOccupiesCrushCell(state, *wall, cell)
                    && sampleZ <= static_cast<double>(wall->level) + 2.001) {
                    return false;
                }
            }
        }

        const auto terrainIt = state.terrainByCell.find(
            runtimeCellKey(cell.x, cell.y)
        );
        const TerrainCell* terrain = terrainIt == state.terrainByCell.end()
            ? nullptr
            : terrainIt->second;
        if (terrain == nullptr) {
            previousCellWasCliff = false;
            continue;
        }
        const RuntimeTerrainInfo info =
            runtimeCachedTerrainInfoForCell(state, *terrain);
        const int level = runtimeEffectiveTerrainLevel(*terrain);
        if (!checkCliffs) {
            continue;
        }
        if (info.cliff) {
            if (static_cast<double>(level) > sampleZ + 0.001) {
                return false;
            }
            previousCellWasCliff = true;
        } else {
            if (previousCellWasCliff
                && static_cast<double>(level) > sampleZ + 0.001) {
                return false;
            }
            previousCellWasCliff = false;
        }
    }
    return true;
}

bool runtimeCrushRepresentativeProvesBlocked(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& crusher,
    const RuntimeCellPoint& cell,
    const std::unordered_set<int>* additionallyIgnoredObjectIds) {
    if (state.tickOccupancyCacheTick != state.tick) return false;
    const int key = runtimeCellKey(cell.x, cell.y);
    if (key < 0) return false;
    const auto found = state.tickOccupiedByCell.find(key);
    if (found == state.tickOccupiedByCell.end()
        || runtimeOccupantIdIsTerrain(found->second)) return false;
    const auto* target = findObjectById(state, found->second);
    if (target == nullptr) return false;
    const RuntimeCrushCandidateList candidate{target};
    return runtimeScanCrushTargetsAtCell(state, crusher, cell, -1,
        additionallyIgnoredObjectIds, &candidate).blocked;
}

bool runtimeObjectUsesOmniCrushApproach(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    return object.alive && object.kind == WorldObjectKind::Unit
        && !object.airborne && object.dockedToObjectId < 0
        && !runtimeGateObjectIsLimbo(object) && object.garrisonHostObjectId < 0
        && object.tankBunkerHostId < 0
        && !runtimeObjectOffGroundByMagBeam(state, object.instanceId)
        && runtimeCachedCapabilityProfileForObject(state, object).omniCrusher
        && runtimeCrusherUsesCrushingMovement(state, object);
}

bool runtimeOmniCrushPendingSegment(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    return runtimeObjectUsesOmniCrushApproach(state, object)
        && (behavior.kind == RuntimeBehaviorKind::Move
            || behavior.kind == RuntimeBehaviorKind::Attack)
        && object.hasRenderPosition && object.renderMoveActive
        && behavior.renderMoveTicksTotal > 0
        && behavior.renderFrom == runtimeCellPointForObject(object)
        && behavior.renderTo != behavior.renderFrom;
}

bool runtimeOmniCrushOwnsOccupiedEndpoint(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& goal) {
    if (!runtimeObjectUsesOmniCrushApproach(state, object)) return false;
    const auto scan = runtimeScanCrushTargetsAtCell(state, object, goal);
    return !scan.blocked && !scan.targetIds.empty();
}

bool runtimeOmniCrushBodiesTouch(
    const WorldObjectInstance& crusher, const WorldObjectInstance& target) {
    const auto sourceOffset = worldObjectSubCellOffset(crusher);
    const auto targetOffset = worldObjectSubCellOffset(target);
    const double dx = runtimeObjectRenderX(crusher) + sourceOffset.x
        - runtimeObjectRenderX(target) - targetOffset.x;
    const double dy = runtimeObjectRenderY(crusher) + sourceOffset.y
        - runtimeObjectRenderY(target) - targetOffset.y;
    const bool sourceBridge = crusher.hasRenderPosition
        ? crusher.renderOnBridge : crusher.onBridge;
    const bool targetBridge = target.hasRenderPosition
        ? target.renderOnBridge : target.onBridge;
    return sourceBridge == targetBridge && dx * dx + dy * dy < 0.25;
}

RuntimeCrushTargetScan runtimeScanOmniCrushPhysicalContacts(
    const RuntimeGameSessionState& state, const WorldObjectInstance& crusher,
    const RuntimeCellPoint& destination) {
    auto scan = runtimeScanCrushTargetsAtCell(state, crusher, destination);
    if (scan.blocked) return scan;
    scan.targetIds.clear();
    // Navigation cells precommit ordinary victims' steps. Retail contacts the
    // actual XY bodies in its ground/bridge layer, including a departing body.
    // This query never makes those victims transparent to path planning.
    for (const auto& target : state.world.getObjects()) {
        if (target.instanceId == crusher.instanceId || !target.alive
            || runtimeGateObjectIsLimbo(target) || target.garrisonHostObjectId >= 0
            || target.tankBunkerHostId >= 0
            || !runtimeOmniCrushBodiesTouch(crusher, target)
            || !runtimeCrusherCanCrushObject(state, crusher, target, destination, true)) continue;
        scan.targetIds.push_back(target.instanceId);
    }
    std::sort(scan.targetIds.begin(), scan.targetIds.end());
    return scan;
}

bool cancelRuntimeBlockedOmniCrushApproach(
    RuntimeGameSessionState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior) {
    if (!runtimeOmniCrushPendingSegment(state, object, behavior)
        || !runtimeScanCrushTargetsAtCell(state, object, behavior.renderTo).blocked)
        return false;
    // A relation or protection may change after the step was accepted. Keep
    // the departure occupancy and freeze the actual body, just as Stop does.
    object.renderMoveActive = false;
    behavior.moveCooldownTicks = 0;
    behavior.renderMoveTicksTotal = behavior.renderMoveTicksRemaining = 0;
    behavior.renderArrivalDistanceAfter = -1.0;
    behavior.moveSegmentFrom = {-1, -1};
    behavior.path.clear();
    behavior.pathRequestPending = false;
    behavior.state = RuntimeBehaviorState::Blocked;
    behavior.nextRepathTick = state.tick + 1;
    return true;
}

const std::unordered_map<int, std::vector<std::size_t>>& runtimeSolidFoundationsByCell(
    const RuntimeGameSessionState& state) {
    const auto& objects = state.world.getObjects();
    if (state.foundationCollisionTick == state.tick
        && state.foundationCollisionObjects == objects.data()
        && state.foundationCollisionObjectCount == objects.size())
        return state.foundationCollisionByCell;
    auto& cells = state.foundationCollisionByCell;
    cells.clear();
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const auto& object = objects[index];
        if (!object.alive || runtimeObjectOffGroundByMagBeam(state, object.instanceId)) continue;
        std::vector<RuntimeCellPoint> footprint;
        if (object.kind == WorldObjectKind::Structure) {
            footprint = runtimeStructureFootprintCellsForObject(state, object);
        } else if (runtimeMissionIsMcvDeployPending(object)) {
            const auto* unit = findRuntimeUnitType(state.rules, object);
            const auto pending = state.behaviors.find(object.instanceId);
            if (!unit || unit->deploysInto.empty() || pending == state.behaviors.end()) continue;
            WorldObjectInstance futureYard;
            futureYard.kind = WorldObjectKind::Structure; futureYard.type = unit->deploysInto;
            const RuntimeCellPoint corner = runtimePlacementTopLeftForAnchor(
                pending->second.moveTarget, runtimeStructureFootprintForObject(state, futureYard));
            futureYard.x = corner.x; futureYard.y = corner.y;
            footprint = runtimeStructureFootprintCellsForObject(state, futureYard);
        } else continue;
        for (const auto& cell : footprint) {
            const int key = runtimeCellKey(cell.x, cell.y);
            if (key >= 0) cells[key].push_back(index);
        }
    }
    state.foundationCollisionTick = state.tick;
    state.foundationCollisionObjects = objects.data();
    state.foundationCollisionObjectCount = objects.size();
    return cells;
}

bool runtimeFoundationBlocksMovement(
    const RuntimeGameSessionState& state, const WorldObjectInstance& mover,
    const RuntimeCellPoint& cell, int allowedOccupiedObjectId,
    bool allowCrushableOccupants, RuntimeGatePassageMode gateMode) {
    const auto& foundations = runtimeSolidFoundationsByCell(state);
    const auto found = foundations.find(runtimeCellKey(cell.x, cell.y));
    if (found == foundations.end()) return false;
    const auto openGates = state.gateIdsByCell.contains(runtimeCellKey(cell.x, cell.y))
        ? runtimeTransparentGateObjectIds(state, mover, gateMode)
        : std::unordered_set<int>{};
    for (const auto index : found->second) {
        const auto& building = state.world.getObjects()[index];
        if (!building.alive || building.instanceId == mover.instanceId
            || openGates.contains(building.instanceId)) continue;
        // Reserve an accepted MCV's complete future foundation through its
        // turn, including diagonal side cells. Stop/death revoke the mission.
        if (runtimeMissionIsMcvDeployPending(building)) return true;
        if (building.instanceId == allowedOccupiedObjectId
            && runtimeCellIsAllowedOccupiedStructureFootprint(
                state, cell, allowedOccupiedObjectId, &mover)) continue;
        if (allowCrushableOccupants && runtimeCrusherUsesCrushingMovement(state, mover)
            && runtimeCrusherCanCrushObject(state, mover, building, cell)) continue;
        return true;
    }
    return false;
}

double runtimeAttackRangeDistanceFromCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& firingCell,
    const WorldObjectInstance& target,
    bool cellRangefinding) {
    return cellRangefinding
        ? runtimeCellDistance(firingCell, runtimeCellPointForObject(target))
        : runtimeCellDistanceToObjectFootprint(state, firingCell, target);
}

int runtimeAttackApproachRangeBucket(double rangeCells) {
    return static_cast<int>(std::lround(std::max(0.0, rangeCells) * 100.0));
}

std::uint64_t runtimeAttackApproachCacheKey(
    const WorldObjectInstance& target,
    double rangeCells,
    bool cellRangefinding) {
    std::uint64_t hash = runtimeStableHashString("attackApproach");
    runtimeStableHashMixString(hash, target.type);
    runtimeStableHashMix(hash, target.instanceId);
    runtimeStableHashMix(hash, target.x);
    runtimeStableHashMix(hash, target.y);
    runtimeStableHashMix(hash, static_cast<int>(target.kind));
    runtimeStableHashMix(hash, runtimeAttackApproachRangeBucket(rangeCells));
    runtimeStableHashMix(hash, cellRangefinding);
    return hash;
}

void resetRuntimeAttackApproachCacheForTick(RuntimeGameSessionState& state) {
    if (state.attackApproachStats.tick == state.tick) {
        return;
    }

    state.attackApproachStats.tick = state.tick;
    state.attackApproachStats.builds = 0;
    state.attackApproachStats.hits = 0;
    state.attackApproachStats.candidates = 0;
    state.attackApproachStats.submitted = 0;
}

const RuntimeAttackApproachCacheEntry& runtimeAttackApproachEntry(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& target,
    double rangeCells,
    bool cellRangefinding) {
    resetRuntimeAttackApproachCacheForTick(state);
    const std::uint64_t key = runtimeAttackApproachCacheKey(
        target,
        rangeCells,
        cellRangefinding
    );
    const auto found = state.attackApproachCache.find(key);
    if (found != state.attackApproachCache.end()) {
        ++state.attackApproachStats.hits;
        return found->second;
    }

    RuntimeAttackApproachCacheEntry entry;
    entry.tick = state.tick;
    entry.targetObjectId = target.instanceId;
    entry.rangeBucket = runtimeAttackApproachRangeBucket(rangeCells);
    std::unordered_set<int> candidateKeys;

    const auto addCandidate =
        [&state, &target, rangeCells, cellRangefinding, &entry, &candidateKeys](
            const RuntimeCellPoint& point,
            bool mapQueryCandidate
        ) {
            const int key = runtimeCellKey(point.x, point.y);
            if (key < 0 || !candidateKeys.insert(key).second) {
                return;
            }

            const auto terrainIt = state.terrainByCell.find(key);
            if (terrainIt == state.terrainByCell.end()
                || terrainIt->second == nullptr) {
                return;
            }

            const RuntimeTerrainInfo info =
                runtimeCachedTerrainInfoForCell(state, *terrainIt->second);
            const double attackDistance = runtimeAttackRangeDistanceFromCell(
                state,
                point,
                target,
                cellRangefinding
            );
            if (!info.passable
                || attackDistance <= 0.001
                || attackDistance > rangeCells + 0.001) {
                return;
            }

            entry.candidates.push_back(point);
            if (mapQueryCandidate) {
                entry.mapQueryCandidates.push_back(point);
            }
        };

    if (state.mapFactsLoaded
        && target.kind == WorldObjectKind::Structure) {
        const MapQuery query(state.mapFacts);
        const MapStructureAnchorFact* anchor =
            runtimeMapQueryStructureAnchor(state, target, query);
        if (anchor != nullptr) {
            entry.mapQueryApproachAvailable = true;
            for (const MapFootprintCellFact& approach :
                    query.getStructureApproachCells(*anchor, "attack")) {
                if (!approach.inBounds || !approach.hasMapCell) {
                    continue;
                }
                addCandidate({approach.x, approach.y}, true);
            }
        }
    }

    const RuntimeStructureFootprint targetFootprint =
        runtimeStructureFootprintForObject(state, target);
    const int targetWidth = std::max(1, targetFootprint.width);
    const int targetHeight = std::max(1, targetFootprint.height);
    const int searchRadius =
        std::max(2, static_cast<int>(std::ceil(rangeCells)) + 3);
    const int minCandidateX = target.x - searchRadius;
    const int maxCandidateX = target.x + targetWidth - 1 + searchRadius;
    const int minCandidateY = target.y - searchRadius;
    const int maxCandidateY = target.y + targetHeight - 1 + searchRadius;
    for (int y = minCandidateY; y <= maxCandidateY; ++y) {
        for (int x = minCandidateX; x <= maxCandidateX; ++x) {
            addCandidate({x, y}, false);
        }
    }

    std::sort(
        entry.candidates.begin(),
        entry.candidates.end(),
        [&state, &target, cellRangefinding](
            const RuntimeCellPoint& left,
            const RuntimeCellPoint& right
        ) {
            const double leftDistance = runtimeAttackRangeDistanceFromCell(
                state,
                left,
                target,
                cellRangefinding
            );
            const double rightDistance = runtimeAttackRangeDistanceFromCell(
                state,
                right,
                target,
                cellRangefinding
            );
            if (leftDistance != rightDistance) {
                return leftDistance < rightDistance;
            }
            if (left.y != right.y) {
                return left.y < right.y;
            }
            return left.x < right.x;
        }
    );

    entry.contextHash =
        runtimeMapQueryApproachContextHash(
            "attack",
            target,
            entry.mapQueryCandidates
        );
    ++state.attackApproachStats.builds;
    state.attackApproachStats.candidates =
        std::max(
            state.attackApproachStats.candidates,
            static_cast<int>(entry.candidates.size())
        );
    const auto inserted =
        state.attackApproachCache.emplace(key, std::move(entry));
    return inserted.first->second;
}

bool runtimeMapInitialVeteranSpecialFlag(const IniFile& mapIni) {
    const std::string value = trimAsciiLocal(
        mapIni.getValue("SpecialFlags", "InitialVeteran", "")
    );
    return !value.empty() && parseRuntimeBool(value);
}

bool runtimeObjectUsesSubmarineReveal(const RuntimeGameSessionState& state,const WorldObjectInstance& object) {
    const auto* unit=findRuntimeUnitType(state.rules,object);
    return unit && unit->naval && unit->underwater && !unit->organic && runtimeObjectCanCloak(state,object);
}

bool runtimeRevealSubmarineForActivity(RuntimeProductionHostState& state,WorldObjectInstance& object,const std::string& reason) {
    if(!runtimeObjectUsesSubmarineReveal(state,object))return false;
    bool changed=runtimeDecloakObject(state,object,reason);
    object.submergeCooldownTicks=std::max(object.submergeCooldownTicks,runtimeObjectCloakTransitionTicks(state,object));
    changed=runtimeSetObjectSubmerged(state,object,false,reason)||changed;
    return changed;
}

bool runtimeRevealObjectAfterDamage(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const std::string& reason) {
    bool changed = runtimeDecloakObject(state, object, reason);
    ensureRuntimeSpiesInitialized(state);
    const RuntimeSpyDisguiseResult damagedDisguise =
        state.spies.notifyDamaged(
            makeRuntimeSpyContext(state),
            object.instanceId,
            state.tick
        );
    changed = applyRuntimeSpyActions(state, damagedDisguise.actions)
        || changed;
    const UnitType* unit = findRuntimeUnitType(state.rules, object);
    if (unit != nullptr && unit->underwater) {
        object.submergeCooldownTicks = runtimeObjectCloakTransitionTicks(
            state,
            object
        );
        changed = runtimeSetObjectSubmerged(
            state,
            object,
            false,
            reason
        ) || changed;
    }
    state.cloakObservedHealthByObject[object.instanceId] = object.health;
    return changed;
}

bool runtimeIsSuperweaponCatalogItem(const RuntimeBuildCatalogItem& item) {
    static const std::unordered_set<std::string> superweaponStructures = {
        "GACSPH",
        "GAWEAT",
        "NAMISL",
        "NAIRON",
        "YAPPET",
        "YAGNTC"
    };
    return superweaponStructures.find(toUpperAsciiLocal(item.typeId))
        != superweaponStructures.end();
}

void applyRuntimeSuperweaponCatalogOption(RuntimeGameSessionState& state) {
    if (state.config.superweapons) {
        return;
    }
    state.economy.catalog.erase(
        std::remove_if(
            state.economy.catalog.begin(),
            state.economy.catalog.end(),
            [](const RuntimeBuildCatalogItem& item) {
                return runtimeIsSuperweaponCatalogItem(item);
            }
        ),
        state.economy.catalog.end()
    );
}

bool advanceRuntimeWeaponCooldowns(RuntimeGameSessionState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeWeaponCooldowns", state.tick);
    bool changed = false;
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (object.radiationEruptionCooldownTicksRemaining > 0) {
            object.radiationEruptionCooldownTicksRemaining = object.health <= 0
                ? 0 : object.radiationEruptionCooldownTicksRemaining - 1;
            changed = true;
        }
        if (object.health <= 0) {
            if (object.weaponCooldownTicksRemaining != 0) {
                object.weaponCooldownTicksRemaining = 0;
                changed = true;
            }
            continue;
        }
        if (object.weaponCooldownTicksRemaining > 0) {
            --object.weaponCooldownTicksRemaining;
            changed = true;
        }
    }
    return changed;
}

std::optional<std::array<double, 2>> runtimeRepairDepotApproachPoint(
    const RuntimeGameSessionState& state, const WorldObjectInstance& unit, const RuntimeCellPoint& target) {
    if (!state.repairDepotsInitialized || unit.kind != WorldObjectKind::Unit || unit.airborne) return std::nullopt;
    const auto* relation = findRuntimeRepairDepotRelationForUnit(state.repairDepots, unit.instanceId);
    if (!relation || relation->phase != RuntimeRepairDepotRelationPhase::Reserved
        || target.x != relation->dockCell.x || target.y != relation->dockCell.y) return std::nullopt;
    const auto* host = findObjectById(state, relation->hostId);
    const auto* building = host ? state.rules.findBuildingType(host->type) : nullptr;
    if (!host || !host->alive || !building || !building->unitRepair
        || relation->dockIndex < 0 || relation->dockIndex >= building->numberOfDocks)
        return std::nullopt;
    const auto domain = building->naval || building->waterBound
        ? RuntimeRepairDepotDomain::Naval : RuntimeRepairDepotDomain::Ground;
    if (relation->domain != domain) return std::nullopt;
    const auto dock = runtimeAircraftDockSpawn(state, *host, relation->dockIndex);
    if (dock.cell != target) return std::nullopt;
    return std::array<double, 2>{dock.renderX, dock.renderY};
}

double runtimeRepairDepotApproachDistanceScale(const RuntimeGameSessionState& state,
    const WorldObjectInstance& unit, const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    if (unit.kind != WorldObjectKind::Unit || unit.airborne) return 1.0;
    const auto dock = runtimeRepairDepotApproachPoint(state, unit, to);
    // The caller has committed the next integer cell but still retains the
    // visible departure point. Include it when leaving an authored half-cell
    // dock, so a longer diagonal departure does not get a one-cell duration.
    const double fromX = unit.hasRenderPosition ? unit.renderX : double(from.x);
    const double fromY = unit.hasRenderPosition ? unit.renderY : double(from.y);
    const double toX = dock ? (*dock)[0] : double(to.x);
    const double toY = dock ? (*dock)[1] : double(to.y);
    if (!dock && fromX == from.x && fromY == from.y) return 1.0;
    // A reserved ship may already occupy the dock's navigation cell. Its
    // remaining authored offset is a real, shorter movement segment.
    if (dock && from == to) return std::hypot(toX-fromX, toY-fromY);
    return std::max(1.0, std::hypot(toX-fromX, toY-fromY)
        / std::max(1.0, std::hypot(double(to.x-from.x), double(to.y-from.y))));
}

bool beginRuntimeRepairDepotSameCellApproach(RuntimeProductionHostState& state,
    WorldObjectInstance& unit, RuntimeObjectBehavior& behavior) {
    const auto current = runtimeCellPointForObject(unit);
    if (behavior.kind != RuntimeBehaviorKind::Move || behavior.moveCooldownTicks > 0
        || current != behavior.moveTarget || unit.kind != WorldObjectKind::Unit
        || unit.airborne || runtimeObjectMovementMode(state, unit) != PathMovementMode::Naval)
        return false;
    const auto dock = runtimeRepairDepotApproachPoint(state, unit, current);
    if (!dock) return false;
    const double fromX = unit.hasRenderPosition ? unit.renderX : double(unit.x);
    const double fromY = unit.hasRenderPosition ? unit.renderY : double(unit.y);
    const double dx = (*dock)[0] - fromX, dy = (*dock)[1] - fromY;
    const double directionScale = std::max(std::abs(dx), std::abs(dy));
    if (directionScale < 1.0e-9) return false;

    // Face the actual offset using the ordinary ROT clock. Integer from/to
    // cells are equal here, so the normal cell-facing helper has no direction.
    const int desiredFacing = runtimeFacingFromDelta(
        static_cast<int>(std::llround(dx / directionScale * 1024.0)),
        static_cast<int>(std::llround(dy / directionScale * 1024.0)));
    unit.facing = unit.facing < 0 ? desiredFacing
        : runtimeAdvanceBodyFacing(state, unit, unit.facing, desiredFacing);
    if (unit.facing != desiredFacing) {
        behavior.state = RuntimeBehaviorState::Cooldown;
        return true;
    }
    behavior.moveCooldownTicks = consumeRuntimeMovementSegmentIntervalTicks(
        state, unit, behavior, current, current);
    beginRuntimeRenderMove(unit, behavior, current, current,
        unit.hasRenderPosition ? unit.renderLevel : static_cast<double>(unit.level),
        unit.hasRenderPosition ? unit.renderOnBridge : unit.onBridge,
        behavior.moveCooldownTicks, &state);
    behavior.state = RuntimeBehaviorState::Moving;
    setRuntimeMissionIntent(state, unit, RuntimeMissionIntent::Moving,
        "repairDepotSameCellApproach");
    ++state.actionSummary.movedThisTick;
    return true;
}

bool runtimeObjectUsesNavalDockPresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& unit) {
    const auto* type = findRuntimeUnitType(state.rules, unit);
    return unit.kind == WorldObjectKind::Unit && !unit.airborne && !unit.onBridge
        && type && type->naval && toLowerAsciiLocal(type->locomotor)
            == "{2bea74e1-7cca-11d3-be14-00104b62a16c}";
}

std::optional<std::array<double, 3>> runtimeNavalDockCommandPose(
    const RuntimeGameSessionState& state, const WorldObjectInstance& unit) {
    if (!runtimeObjectUsesNavalDockPresentation(state, unit) || !unit.hasRenderPosition)
        return std::nullopt;
    if (unit.renderMoveActive) {
        const auto found = state.behaviors.find(unit.instanceId);
        if (found == state.behaviors.end() || found->second.kind != RuntimeBehaviorKind::Move
            || found->second.moveCooldownTicks <= 0) return std::nullopt;
        const auto& move = found->second;
        if (move.renderFromOffsetX == 0.0 && move.renderFromOffsetY == 0.0
            && move.renderToOffsetX == 0.0 && move.renderToOffsetY == 0.0) return std::nullopt;
    } else if (unit.renderX == unit.x && unit.renderY == unit.y) return std::nullopt;
    return std::array<double, 3>{unit.renderX, unit.renderY, unit.renderLevel};
}

std::optional<RuntimeObjectBehavior> runtimeNavalDockCommandContinuation(
    const RuntimeGameSessionState& state, const WorldObjectInstance& unit) {
    if (!unit.renderMoveActive || !runtimeNavalDockCommandPose(state, unit)) return std::nullopt;
    return state.behaviors.at(unit.instanceId);
}

void restoreRuntimeNavalDockCommandStopPose(WorldObjectInstance& unit,
    const std::optional<std::array<double, 3>>& pose) {
    if (!pose) return;
    unit.hasRenderPosition = true;
    unit.renderMoveActive = false;
    unit.renderX = (*pose)[0]; unit.renderY = (*pose)[1]; unit.renderLevel = (*pose)[2];
    unit.renderOnBridge = false;
}

bool applyRuntimeRepairDepotDockPose(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& unit,
    const WorldObjectInstance& host,
    int dockIndex) {
    const auto* building = state.rules.findBuildingType(host.type);
    if (!unit.alive || unit.kind != WorldObjectKind::Unit || unit.airborne || !host.alive
        || !building || !building->unitRepair
        || unit.dockedToObjectId != host.instanceId || dockIndex < 0
        || dockIndex >= building->numberOfDocks) return false;
    const auto dock = runtimeAircraftDockSpawn(state, host, dockIndex);
    if (unit.x != dock.cell.x || unit.y != dock.cell.y) return false;
    const bool changed = !unit.hasRenderPosition || unit.renderMoveActive
        || unit.renderX != dock.renderX || unit.renderY != dock.renderY
        || unit.renderLevel != static_cast<double>(unit.level)
        || unit.renderOnBridge != unit.onBridge;
    unit.hasRenderPosition = true;
    unit.renderX = dock.renderX;
    unit.renderY = dock.renderY;
    unit.renderMoveActive = false;
    unit.renderLevel = static_cast<double>(unit.level);
    unit.renderOnBridge = unit.onBridge;
    return changed;
}

bool refreshRuntimeRepairDepotDockPoses(RuntimeProductionHostState& state,
    const RuntimeProductionHostState* authoredState) {
    bool changed = false;
    for (const auto& relation : state.repairDepots.relations()) {
        if (relation.phase != RuntimeRepairDepotRelationPhase::Docked) continue;
        auto* unit = findMutableObjectById(state, relation.unitId);
        const auto* host = findObjectById(state, relation.hostId);
        const auto& authored = authoredState ? *authoredState : state;
        const auto* building = host ? authored.rules.findBuildingType(host->type) : nullptr;
        if (!building || relation.domain != (building->naval || building->waterBound
                ? RuntimeRepairDepotDomain::Naval : RuntimeRepairDepotDomain::Ground)) continue;
        if (unit && host)
            // Ownership transactions intentionally omit the immutable ART
            // database from their scratch state. Read the live authored dock
            // offsets while applying the pose to the staged world objects.
            changed = applyRuntimeRepairDepotDockPose(authored,
                *unit, *host, relation.dockIndex) || changed;
    }
    if (changed) state.repaintRequested = true;
    return changed;
}

bool runtimeGarrisonOwnersAllowEntry(
    const WorldObjectInstance& occupier,
    const WorldObjectInstance& host) {
    return occupier.owner == host.owner
        || (host.garrisonOccupantIds.empty()
            && (runtimeOwnerIsNeutralForAutoTarget(host.owner)
                || toUpperAsciiLocal(trimAsciiLocal(host.owner)) == "CIVILIANS"));
}

bool runtimeOrdinaryAttackRelationLegal(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    bool forceFire) {
    if (forceFire) return true;
    if (runtimeOwnersAreAllied(state, attacker.owner, target.owner)) return false;

    // An occupier preserves a usable civilian house even when entry is
    // temporarily blocked by reservations or terrain. Vehicles and enemy
    // garrisons use the ordinary attack relationship; Ctrl still forces fire.
    if (attacker.kind == WorldObjectKind::Infantry
        && target.kind == WorldObjectKind::Structure
        && runtimeGarrisonOwnersAllowEntry(attacker, target)) {
        const UnitType* infantry = findRuntimeUnitType(state.rules, attacker);
        const BuildingType* building = findRuntimeBuildingType(state.rules, target);
        if (infantry != nullptr && infantry->occupier
            && building != nullptr && building->canBeOccupied
            && building->maxNumberOccupants > 0
            && target.health > static_cast<double>(std::max(1, building->strength))
                * state.rules.getSpecialMechanismRules().conditionRed) {
            return false;
        }
    }
    return true;
}

std::string runtimeGarrisonEntryVetoReason(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& occupier) {
    // The relation survives Move/Stop orders and permanent Dominator control;
    // the current mission string and the new owner do not identify it.
    return state.specialMechanisms.mindControlLinks().contains(occupier.instanceId)
        ? "occupant is mind-controlled" : "";
}

std::string serializeRuntimeProjectileEffect(
    const RuntimeProjectileEffect& projectile) {
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    bool first = true;
    const auto write = [&output, &first](const auto& value) {
        if (!first) {
            output << '|';
        }
        first = false;
        output << value;
    };
    const auto writeBool = [&write](bool value) {
        write(runtimeBoolText(value));
    };
    const auto writeString = [&write](const std::string& value) {
        write(runtimeSaveField(value));
    };

    write(projectile.actorId);
    write(projectile.parentActorId);
    write(projectile.generation);
    write(projectile.attackerObjectId);
    write(projectile.targetObjectId);
    write(projectile.sourceAnchorObjectId);
    writeString(projectile.owner);
    write(projectile.fromCell.x);
    write(projectile.fromCell.y);
    write(projectile.toCell.x);
    write(projectile.toCell.y);
    write(projectile.currentCell.x);
    write(projectile.currentCell.y);
    writeBool(projectile.hasSourcePosition);
    write(projectile.sourceX);
    write(projectile.sourceY);
    write(projectile.sourceZ);
    write(projectile.sourceScreenOffsetX);
    write(projectile.sourceScreenOffsetY);
    writeBool(projectile.hasTargetPosition);
    write(projectile.targetX);
    write(projectile.targetY);
    write(projectile.targetZ);
    write(projectile.fromLevel);
    write(projectile.toLevel);
    write(projectile.currentLevel);
    write(projectile.attackerFacing);
    write(projectile.targetFacing);
    write(projectile.spawnTick);
    write(projectile.impactTick);
    write(projectile.elapsedTicks);
    write(projectile.damage);
    write(projectile.veteranDamageMultiplier);
    writeBool(projectile.spawnedMissileDamage);
    write(projectile.spawnedMissileBaseDamage);
    write(projectile.totalTicks);
    write(projectile.remainingTicks);
    write(static_cast<int>(projectile.lifecycle));
    writeString(projectile.lifecycleState);
    writeString(projectile.renderIntent);
    writeString(projectile.weaponId);
    writeString(projectile.weaponSlot);
    writeString(projectile.projectileId);
    writeString(projectile.projectileImageId);
    writeString(projectile.warheadId);
    writeString(projectile.weaponAnimId);
    writeString(projectile.impactAnimId);
    writeString(projectile.trailerAnimId);
    writeString(projectile.trailerIntentId);
    writeString(projectile.warheadImpactIntentId);
    writeString(projectile.reportSoundId);
    writeBool(projectile.arcing);
    writeBool(projectile.vertical);
    write(projectile.acceleration);
    writeBool(projectile.high);
    writeBool(projectile.veryHigh);
    writeBool(projectile.shadow);
    writeBool(projectile.animates);
    writeBool(projectile.aa);
    writeBool(projectile.ag);
    write(projectile.cluster);
    writeBool(projectile.bright);
    writeBool(projectile.laser);
    writeBool(projectile.houseColorLaser);
    writeBool(projectile.electricBolt);
    writeBool(projectile.alternateElectricColor);
    writeString(projectile.electricColor);
    writeBool(projectile.shrapnel);
    writeBool(projectile.shrapnelReleased);
    write(projectile.laserDuration);
    write(projectile.laserThickness);
    write(projectile.cellSpread);
    write(projectile.percentAtMax);
    writeBool(projectile.affectsAllies);
    writeBool(projectile.affectsEnemies);
    writeString(projectile.particleId);
    write(projectile.progress);
    write(projectile.currentX);
    write(projectile.currentY);
    write(projectile.currentZ);
    write(projectile.speedCellsPerTick);
    write(projectile.rot);
    write(projectile.deathWeaponCascadeId);
    write(projectile.deathWeaponRequestId);
    write(projectile.deathWeaponCascadeDepth);
    write(projectile.launchSourceKind);
    writeString(projectile.launchSourceType);
    writeBool(projectile.launchSourceAuthoredAircraft);
    writeBool(projectile.launchSourceValidated);
    write(projectile.airstrikeStrikeId);
    write(projectile.airstrikePlaneToken);
    writeBool(projectile.particleDamagePulse);
    writeBool(projectile.suppressWarheadParticleSpawn);
    writeString(serializeRuntimeVoxelDebris(projectile.voxelDebris));
    writeBool(projectile.homingInitialized);
    write(projectile.homingYaw);
    write(projectile.homingPitch);
    write(projectile.sourceNativeFixedX);
    write(projectile.sourceNativeFixedY);
    return output.str();
}

bool parseRuntimeProjectileEffect(
    const std::string& value,
    RuntimeProjectileEffect& projectile,
    int schema,
    bool allowExpired) {
    const std::vector<std::string> tokens = splitRuntimeSaveFields(value);
    const std::size_t expectedFieldCount =
        schema == runtimeProjectileSaveSchemaV1
            ? runtimeProjectileSaveFieldCountV1
            : schema == runtimeProjectileSaveSchemaV2
                ? runtimeProjectileSaveFieldCountV2
                : schema == runtimeProjectileSaveSchemaV3
                    ? runtimeProjectileSaveFieldCountV3
                    : schema == runtimeProjectileSaveSchemaV4
                        ? runtimeProjectileSaveFieldCountV4
                        : schema == runtimeProjectileSaveSchemaV5
                            ? runtimeProjectileSaveFieldCountV5
                            : schema == runtimeProjectileSaveSchemaV6
                                ? runtimeProjectileSaveFieldCountV6 : 0U;
    if (expectedFieldCount == 0U || tokens.size() != expectedFieldCount) {
        return false;
    }

    projectile.deathWeaponCascadeId = 0;
    projectile.deathWeaponRequestId = 0;
    projectile.deathWeaponCascadeDepth = 0;
    projectile.launchSourceKind = -1;
    projectile.launchSourceType.clear();
    projectile.launchSourceAuthoredAircraft = false;
    projectile.launchSourceValidated = false;
    projectile.airstrikeStrikeId = 0;
    projectile.airstrikePlaneToken = 0;
    projectile.particleDamagePulse = false;
    projectile.suppressWarheadParticleSpawn = false;
    projectile.voxelDebris = {};
    projectile.homingInitialized = false;
    projectile.homingYaw = projectile.homingPitch = 0.0;
    projectile.sourceNativeFixedX = projectile.sourceNativeFixedY = 0;
    std::string debrisRecord;

    std::size_t cursor = 0;
    const auto readInt = [&tokens, &cursor](int& result) {
        return cursor < tokens.size()
            && parseRuntimeInt(tokens[cursor++], result);
    };
    const auto readBool = [&tokens, &cursor](bool& result) {
        return cursor < tokens.size()
            && parseRuntimeStrictBool(tokens[cursor++], result);
    };
    const auto readDouble = [&tokens, &cursor](double& result) {
        return cursor < tokens.size()
            && parseRuntimeFiniteDouble(tokens[cursor++], result);
    };
    const auto readString = [&tokens, &cursor](std::string& result) {
        if (cursor >= tokens.size()) {
            return false;
        }
        result = tokens[cursor++];
        return true;
    };
    const auto readUint64 = [&tokens, &cursor](std::uint64_t& result) {
        return cursor < tokens.size()
            && parseRuntimeUint64Strict(tokens[cursor++], result);
    };

    int lifecycle = 0;
    std::uint64_t deathWeaponCascadeDepth = 0;
    if (!readInt(projectile.actorId)
        || !readInt(projectile.parentActorId)
        || !readInt(projectile.generation)
        || !readInt(projectile.attackerObjectId)
        || !readInt(projectile.targetObjectId)
        || !readInt(projectile.sourceAnchorObjectId)
        || !readString(projectile.owner)
        || !readInt(projectile.fromCell.x)
        || !readInt(projectile.fromCell.y)
        || !readInt(projectile.toCell.x)
        || !readInt(projectile.toCell.y)
        || !readInt(projectile.currentCell.x)
        || !readInt(projectile.currentCell.y)
        || !readBool(projectile.hasSourcePosition)
        || !readDouble(projectile.sourceX)
        || !readDouble(projectile.sourceY)
        || !readDouble(projectile.sourceZ)
        || !readInt(projectile.sourceScreenOffsetX)
        || !readInt(projectile.sourceScreenOffsetY)
        || !readBool(projectile.hasTargetPosition)
        || !readDouble(projectile.targetX)
        || !readDouble(projectile.targetY)
        || !readDouble(projectile.targetZ)
        || !readInt(projectile.fromLevel)
        || !readInt(projectile.toLevel)
        || !readInt(projectile.currentLevel)
        || !readInt(projectile.attackerFacing)
        || !readInt(projectile.targetFacing)
        || !readInt(projectile.spawnTick)
        || !readInt(projectile.impactTick)
        || !readInt(projectile.elapsedTicks)
        || !readInt(projectile.damage)
        || !readDouble(projectile.veteranDamageMultiplier)
        || !readBool(projectile.spawnedMissileDamage)
        || !readInt(projectile.spawnedMissileBaseDamage)
        || !readInt(projectile.totalTicks)
        || !readInt(projectile.remainingTicks)
        || !readInt(lifecycle)
        || !readString(projectile.lifecycleState)
        || !readString(projectile.renderIntent)
        || !readString(projectile.weaponId)
        || !readString(projectile.weaponSlot)
        || !readString(projectile.projectileId)
        || !readString(projectile.projectileImageId)
        || !readString(projectile.warheadId)
        || !readString(projectile.weaponAnimId)
        || !readString(projectile.impactAnimId)
        || !readString(projectile.trailerAnimId)
        || !readString(projectile.trailerIntentId)
        || !readString(projectile.warheadImpactIntentId)
        || !readString(projectile.reportSoundId)
        || !readBool(projectile.arcing)
        || !readBool(projectile.vertical)
        || !readInt(projectile.acceleration)
        || !readBool(projectile.high)
        || !readBool(projectile.veryHigh)
        || !readBool(projectile.shadow)
        || !readBool(projectile.animates)
        || !readBool(projectile.aa)
        || !readBool(projectile.ag)
        || !readInt(projectile.cluster)
        || !readBool(projectile.bright)
        || !readBool(projectile.laser)
        || !readBool(projectile.houseColorLaser)
        || !readBool(projectile.electricBolt)
        || !readBool(projectile.alternateElectricColor)
        || !readString(projectile.electricColor)
        || !readBool(projectile.shrapnel)
        || !readBool(projectile.shrapnelReleased)
        || !readInt(projectile.laserDuration)
        || !readInt(projectile.laserThickness)
        || !readDouble(projectile.cellSpread)
        || !readDouble(projectile.percentAtMax)
        || !readBool(projectile.affectsAllies)
        || !readBool(projectile.affectsEnemies)
        || !readString(projectile.particleId)
        || !readDouble(projectile.progress)
        || !readDouble(projectile.currentX)
        || !readDouble(projectile.currentY)
        || !readDouble(projectile.currentZ)
        || !readDouble(projectile.speedCellsPerTick)
        || !readInt(projectile.rot)
        || (schema >= runtimeProjectileSaveSchemaV2
            && (!readUint64(projectile.deathWeaponCascadeId)
                || !readUint64(projectile.deathWeaponRequestId)
                || !readUint64(deathWeaponCascadeDepth)))
        || (schema >= runtimeProjectileSaveSchemaV3
            && (!readInt(projectile.launchSourceKind)
                || !readString(projectile.launchSourceType)
                || !readBool(projectile.launchSourceAuthoredAircraft)
                || !readBool(projectile.launchSourceValidated)
                || !readInt(projectile.airstrikeStrikeId)
                || !readInt(projectile.airstrikePlaneToken)
                || !readBool(projectile.particleDamagePulse)
                || !readBool(projectile.suppressWarheadParticleSpawn)))
        || (schema >= runtimeProjectileSaveSchemaV4
            && (!readString(debrisRecord) || !parseRuntimeVoxelDebris(debrisRecord, projectile.voxelDebris)))
        || (schema >= runtimeProjectileSaveSchemaV5
            && (!readBool(projectile.homingInitialized) || !readDouble(projectile.homingYaw)
                || !readDouble(projectile.homingPitch)))
        || (schema >= runtimeProjectileSaveSchemaV6
            && (!readInt(projectile.sourceNativeFixedX) || !readInt(projectile.sourceNativeFixedY)
                || std::abs(static_cast<long long>(projectile.sourceNativeFixedX)) > 65536
                || std::abs(static_cast<long long>(projectile.sourceNativeFixedY)) > 65536))
        || cursor != tokens.size()) {
        return false;
    }

    if (std::abs(projectile.homingYaw) > 3.141592653589794
        || std::abs(projectile.homingPitch) > 1.570796326794897
        || (!projectile.homingInitialized && (projectile.homingYaw != 0.0 || projectile.homingPitch != 0.0))) return false;
    if (projectile.voxelDebris.enabled != (projectile.renderIntent == "unitVoxelDebris")) return false;
    if (projectile.voxelDebris.enabled) {
        const auto& pos = projectile.voxelDebris.position;
        if (std::abs(projectile.currentX - pos[0] / 256.0) > 1e-8
            || std::abs(projectile.currentY - pos[1] / 256.0) > 1e-8
            || std::abs(projectile.currentZ - pos[2] * runtimeDebrisHeightPerLepton) > 1e-8) return false;
    }
    if (deathWeaponCascadeDepth
        > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    projectile.deathWeaponCascadeDepth =
        static_cast<std::uint32_t>(deathWeaponCascadeDepth);

    const bool noDeathWeaponLineage =
        projectile.deathWeaponCascadeId == 0
        && projectile.deathWeaponRequestId == 0
        && projectile.deathWeaponCascadeDepth == 0;
    const bool validDeathWeaponLineage =
        projectile.deathWeaponCascadeId > 0
        && projectile.deathWeaponRequestId
            >= projectile.deathWeaponCascadeId
        && projectile.deathWeaponCascadeDepth < 16U;
    const bool noLaunchProvenance = !projectile.launchSourceValidated
        && projectile.launchSourceKind == -1
        && projectile.launchSourceType.empty()
        && !projectile.launchSourceAuthoredAircraft;
    const bool validLaunchProvenance = projectile.launchSourceValidated
        && projectile.attackerObjectId >= 0
        && !trimAsciiLocal(projectile.owner).empty()
        && projectile.launchSourceKind
            >= static_cast<int>(WorldObjectKind::Structure)
        && projectile.launchSourceKind
            <= static_cast<int>(WorldObjectKind::Infantry)
        && !trimAsciiLocal(projectile.launchSourceType).empty()
        && projectile.launchSourceType.size() <= 128U
        && (!projectile.launchSourceAuthoredAircraft
            || projectile.launchSourceKind
                == static_cast<int>(WorldObjectKind::Unit));
    const bool noAirstrikeAttribution = projectile.airstrikeStrikeId == 0
        && projectile.airstrikePlaneToken == 0;
    const bool validAirstrikeAttribution = projectile.airstrikeStrikeId > 0
        && projectile.airstrikePlaneToken > 0;
    if (projectile.actorId <= 0
        || (projectile.parentActorId < 0 && projectile.parentActorId != -1)
        || projectile.parentActorId == 0
        || projectile.generation < 0
        || projectile.generation > 4
        || lifecycle < static_cast<int>(RuntimeProjectileLifecycleState::Spawned)
        || lifecycle > static_cast<int>(
            allowExpired
                ? RuntimeProjectileLifecycleState::Expired
                : RuntimeProjectileLifecycleState::Traveling
        )
        || projectile.totalTicks <= 0
        || projectile.remainingTicks < 0
        || projectile.remainingTicks > projectile.totalTicks
        || projectile.elapsedTicks < 0
        || projectile.elapsedTicks > projectile.totalTicks
        || projectile.elapsedTicks
            != projectile.totalTicks - projectile.remainingTicks
        || static_cast<long long>(projectile.spawnTick)
                + static_cast<long long>(projectile.totalTicks)
            != static_cast<long long>(projectile.impactTick)
        || projectile.veteranDamageMultiplier < 0.0
        || projectile.laserDuration < 0
        || projectile.laserThickness < 1
        || projectile.laserThickness > 32
        || projectile.cellSpread < 0.0
        || projectile.progress < 0.0
        || projectile.progress > 1.0
        || projectile.speedCellsPerTick < 0.0
        || (!noDeathWeaponLineage && !validDeathWeaponLineage)
        || (!noLaunchProvenance && !validLaunchProvenance)
        || (!noAirstrikeAttribution && !validAirstrikeAttribution)
        || (projectile.particleDamagePulse
            && (!projectile.suppressWarheadParticleSpawn
                || validAirstrikeAttribution))) {
        return false;
    }

    projectile.lifecycle =
        static_cast<RuntimeProjectileLifecycleState>(lifecycle);
    const bool expired = projectile.lifecycle
        == RuntimeProjectileLifecycleState::Expired;
    if ((!expired && projectile.remainingTicks <= 0)
        || (expired
            && (!allowExpired
                || projectile.remainingTicks != 0
                || projectile.elapsedTicks != projectile.totalTicks))
        || (!expired
            && (projectile.lifecycle == RuntimeProjectileLifecycleState::Spawned)
                != (projectile.elapsedTicks == 0))) {
        return false;
    }
    if (toLowerAsciiLocal(trimAsciiLocal(projectile.lifecycleState))
        != toLowerAsciiLocal(runtimeProjectileLifecycleName(
            projectile.lifecycle
        ))) {
        return false;
    }
    return true;
}

bool parseRuntimeProjectilesFromSave(
    const IniFile& ini,
    RuntimeProjectileSaveCandidate& candidate,
    std::string& errorMessage) {
    RuntimeProjectileSaveCandidate parsed;

    if (ini.getSection("Projectiles") == nullptr) {
        candidate = std::move(parsed);
        return true;
    }

    int schema = 0;
    int count = 0;
    int savedNextActorId = 0;
    if (!parseRuntimeInt(ini.getValue("Projectiles", "Schema", ""), schema)
        || (schema != runtimeProjectileSaveSchemaV1
            && schema != runtimeProjectileSaveSchemaV2
            && schema != runtimeProjectileSaveSchemaV3
            && schema != runtimeProjectileSaveSchemaV4
            && schema != runtimeProjectileSaveSchemaV5
            && schema != runtimeProjectileSaveSchemaV6)
        || !parseRuntimeInt(ini.getValue("Projectiles", "Count", ""), count)
        || count < 0
        || count > 100000
        || !parseRuntimeInt(
            ini.getValue("Projectiles", "NextActorId", ""),
            savedNextActorId
        )
        || savedNextActorId <= 0
        || savedNextActorId >= std::numeric_limits<int>::max()) {
        errorMessage = "invalid [Projectiles] header";
        return false;
    }

    std::vector<RuntimeProjectileEffect> restored;
    restored.reserve(static_cast<std::size_t>(count));
    std::unordered_set<int> actorIds;
    actorIds.reserve(static_cast<std::size_t>(count));
    int maximumReferencedActorId = 0;
    for (int index = 0; index < count; ++index) {
        RuntimeProjectileEffect projectile;
        if (!parseRuntimeProjectileEffect(
                ini.getValue("Projectiles", std::to_string(index), ""),
                projectile,
                schema
            )
            || !actorIds.insert(projectile.actorId).second) {
            errorMessage = "invalid [Projectiles] record "
                + std::to_string(index);
            return false;
        }
        maximumReferencedActorId = std::max(
            maximumReferencedActorId,
            projectile.actorId
        );
        if (projectile.parentActorId > 0) {
            maximumReferencedActorId = std::max(
                maximumReferencedActorId,
                projectile.parentActorId
            );
        }
        restored.push_back(std::move(projectile));
    }
    if (maximumReferencedActorId >= std::numeric_limits<int>::max() - 1) {
        errorMessage = "[Projectiles] actor id overflow";
        return false;
    }

    parsed.projectiles = std::move(restored);
    parsed.nextActorId = std::max(
        savedNextActorId,
        maximumReferencedActorId + 1
    );
    parsed.schema = schema;
    candidate = std::move(parsed);
    return true;
}

bool runtimeProjectileLaunchRegistrationValid(
    const RulesDatabase& rules,
    int sourceKind,
    const std::string& sourceType,
    bool authoredAircraft) {
    const std::string type = trimAsciiLocal(sourceType);
    if (type.empty()) {
        return false;
    }
    if (sourceKind == static_cast<int>(WorldObjectKind::Structure)) {
        return !authoredAircraft
            && findRuntimeBuildingTypeById(rules, type) != nullptr;
    }
    if (sourceKind == static_cast<int>(WorldObjectKind::Infantry)) {
        return !authoredAircraft
            && rules.findUnitType("InfantryType", type) != nullptr;
    }
    if (sourceKind != static_cast<int>(WorldObjectKind::Unit)) {
        return false;
    }
    const bool exactVehicle =
        rules.findUnitType("UnitType", type) != nullptr;
    const bool exactAircraft =
        rules.findUnitType("AircraftType", type) != nullptr;
    // Authored aircraft carry an explicit save discriminator. A legacy Unit
    // record still cannot be guessed when the same id is registered in both
    // namespaces.
    return authoredAircraft ? exactAircraft : exactVehicle;
}

bool runtimeProjectileLaunchIdentityForSavedObject(
    const RulesDatabase& rules,
    const WorldObjectInstance& object,
    int& sourceKind,
    std::string& sourceType,
    bool& authoredAircraft) {
    WorldObjectInstance resolved = object;
    if (!runtimeRestoreExactSavedObjectRegistration(rules, resolved, true)) {
        return false;
    }
    sourceKind = static_cast<int>(resolved.kind);
    sourceType = resolved.type;
    authoredAircraft = resolved.authoredAircraft;
    return runtimeProjectileLaunchRegistrationValid(
        rules,
        sourceKind,
        sourceType,
        authoredAircraft
    );
}

bool validateAndCompleteRuntimeProjectileLaunchProvenance(
    const RuntimeGameSessionState& liveState,
    RuntimeProjectileSaveCandidate& candidate,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeDeathWeaponSystemState& deathWeapons,
    std::string& errorMessage,
    const RuntimeAirstrikeSystemState* airstrikeAuthority) {
    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    objectsById.reserve(savedObjects.size());
    for (const WorldObjectInstance& object : savedObjects) {
        objectsById.emplace(object.instanceId, &object);
    }
    std::unordered_map<int, RuntimeProjectileEffect*> projectilesByActor;
    projectilesByActor.reserve(candidate.projectiles.size());
    for (RuntimeProjectileEffect& projectile : candidate.projectiles) {
        projectilesByActor.emplace(projectile.actorId, &projectile);
    }
    std::unordered_map<std::uint64_t,
        const RuntimeDeathWeaponTriggeredObject*> deathByRequest;
    for (const RuntimeDeathWeaponTriggeredObject& triggered :
            deathWeapons.triggeredObjects()) {
        if (triggered.emittedRequestId > 0) {
            deathByRequest.emplace(triggered.emittedRequestId, &triggered);
        }
    }

    // A projectile actor is either a root or a strict, monotonic child.  The
    // actor-id ordering permits an already-expired parent to be absent while
    // still making cycles impossible.  When the parent remains in flight its
    // complete causal identity must agree before legacy provenance can be
    // inherited below.
    for (const RuntimeProjectileEffect& projectile : candidate.projectiles) {
        const bool root = projectile.parentActorId == -1;
        if (root != (projectile.generation == 0)
            || (!root
                && (projectile.parentActorId <= 0
                    || projectile.parentActorId >= projectile.actorId))) {
            errorMessage = "invalid projectile parent/generation identity";
            return false;
        }
        const auto parent = projectilesByActor.find(projectile.parentActorId);
        if (parent == projectilesByActor.end() || parent->second == nullptr) {
            continue;
        }
        if (projectile.attackerObjectId
                != parent->second->attackerObjectId
            || runtimeRepairDepotCanonicalOwnerId(
                    liveState,
                    projectile.owner
                ) != runtimeRepairDepotCanonicalOwnerId(
                    liveState,
                    parent->second->owner
                )
            || projectile.generation != parent->second->generation + 1) {
            errorMessage = "projectile child causal identity mismatch";
            return false;
        }
    }

    if (candidate.schema == runtimeProjectileSaveSchemaV1
        || candidate.schema == runtimeProjectileSaveSchemaV2) {
        for (RuntimeProjectileEffect& projectile : candidate.projectiles) {
            const auto activeParent = projectilesByActor.find(
                projectile.parentActorId
            );
            if (activeParent != projectilesByActor.end()
                && activeParent->second != nullptr) {
                continue;
            }
            const auto savedSource = objectsById.find(
                projectile.attackerObjectId
            );
            int sourceKind = -1;
            std::string sourceType;
            bool authoredAircraft = false;
            bool completed = savedSource != objectsById.end()
                && savedSource->second != nullptr
                && toLowerAsciiLocal(trimAsciiLocal(
                    savedSource->second->owner
                )) == toLowerAsciiLocal(trimAsciiLocal(projectile.owner))
                && runtimeProjectileLaunchIdentityForSavedObject(
                    liveState.rules,
                    *savedSource->second,
                    sourceKind,
                    sourceType,
                    authoredAircraft
                );
            if (!completed && projectile.deathWeaponRequestId > 0) {
                const auto triggered = deathByRequest.find(
                    projectile.deathWeaponRequestId
                );
                if (triggered != deathByRequest.end()
                    && triggered->second != nullptr
                    && triggered->second->objectId
                        == projectile.attackerObjectId) {
                    sourceKind = static_cast<int>(triggered->second->kind);
                    sourceType = triggered->second->type;
                    authoredAircraft = triggered->second->authoredAircraft;
                    completed = runtimeProjectileLaunchRegistrationValid(
                        liveState.rules,
                        sourceKind,
                        sourceType,
                        authoredAircraft
                    );
                }
            }
            if (completed) {
                projectile.launchSourceKind = sourceKind;
                projectile.launchSourceType = sourceType;
                projectile.launchSourceAuthoredAircraft = authoredAircraft;
                projectile.launchSourceValidated = true;
            }
        }
        bool inherited = true;
        while (inherited) {
            inherited = false;
            for (RuntimeProjectileEffect& projectile : candidate.projectiles) {
                if (projectile.launchSourceValidated
                    || projectile.parentActorId <= 0) {
                    continue;
                }
                const auto parent = projectilesByActor.find(
                    projectile.parentActorId
                );
                if (parent == projectilesByActor.end()
                    || parent->second == nullptr
                    || !parent->second->launchSourceValidated
                    || projectile.attackerObjectId
                        != parent->second->attackerObjectId
                    || runtimeRepairDepotCanonicalOwnerId(
                            liveState,
                            projectile.owner
                        ) != runtimeRepairDepotCanonicalOwnerId(
                            liveState,
                            parent->second->owner
                        )
                    || projectile.generation
                        != parent->second->generation + 1) {
                    continue;
                }
                projectile.launchSourceKind =
                    parent->second->launchSourceKind;
                projectile.launchSourceType =
                    parent->second->launchSourceType;
                projectile.launchSourceAuthoredAircraft =
                    parent->second->launchSourceAuthoredAircraft;
                projectile.launchSourceValidated = true;
                inherited = true;
            }
        }
    }

    for (const RuntimeProjectileEffect& projectile : candidate.projectiles) {
        if ((airstrikeAuthority == nullptr
                && (projectile.airstrikeStrikeId != 0
                    || projectile.airstrikePlaneToken != 0))
            || projectile.particleDamagePulse
            || projectile.suppressWarheadParticleSpawn) {
            errorMessage =
                "projectile producer provenance has no host authority";
            return false;
        }
        const auto parent = projectilesByActor.find(projectile.parentActorId);
        if (parent != projectilesByActor.end()
            && parent->second != nullptr
            && (projectile.attackerObjectId
                    != parent->second->attackerObjectId
                || runtimeRepairDepotCanonicalOwnerId(
                        liveState,
                        projectile.owner
                    ) != runtimeRepairDepotCanonicalOwnerId(
                        liveState,
                        parent->second->owner
                    )
                || projectile.generation
                    != parent->second->generation + 1
                || projectile.launchSourceKind
                    != parent->second->launchSourceKind
                || projectile.launchSourceAuthoredAircraft
                    != parent->second->launchSourceAuthoredAircraft
                || projectile.launchSourceValidated
                    != parent->second->launchSourceValidated
                || toLowerAsciiLocal(trimAsciiLocal(
                    projectile.launchSourceType
                )) != toLowerAsciiLocal(trimAsciiLocal(
                    parent->second->launchSourceType
                ))
                || projectile.airstrikeStrikeId
                    != parent->second->airstrikeStrikeId
                || projectile.airstrikePlaneToken
                    != parent->second->airstrikePlaneToken
                || projectile.particleDamagePulse
                    != parent->second->particleDamagePulse
                || projectile.suppressWarheadParticleSpawn
                    != parent->second->suppressWarheadParticleSpawn)) {
            errorMessage = "projectile child causal provenance mismatch";
            return false;
        }
        if (!projectile.launchSourceValidated) {
            continue;
        }
        if (!runtimeProjectileLaunchRegistrationValid(
                liveState.rules,
                projectile.launchSourceKind,
                projectile.launchSourceType,
                projectile.launchSourceAuthoredAircraft
            )) {
            errorMessage = "projectile launch source is not exactly registered";
            return false;
        }
        const auto savedSource = objectsById.find(
            projectile.attackerObjectId
        );
        if (savedSource != objectsById.end()
            && savedSource->second != nullptr) {
            int sourceKind = -1;
            std::string sourceType;
            bool authoredAircraft = false;
            if (!runtimeProjectileLaunchIdentityForSavedObject(
                    liveState.rules,
                    *savedSource->second,
                    sourceKind,
                    sourceType,
                    authoredAircraft
                )
                || sourceKind != projectile.launchSourceKind
                || authoredAircraft
                    != projectile.launchSourceAuthoredAircraft
                || toLowerAsciiLocal(trimAsciiLocal(sourceType))
                    != toLowerAsciiLocal(trimAsciiLocal(
                        projectile.launchSourceType
                    ))
                || toLowerAsciiLocal(trimAsciiLocal(
                    savedSource->second->owner
                )) != toLowerAsciiLocal(trimAsciiLocal(projectile.owner))) {
                errorMessage =
                    "projectile launch source mismatches saved object identity";
                return false;
            }
        }
    }
    return true;
}

void writeRuntimePublicBuildingLocations(const RuntimeGameSessionState& state, std::ostream& output) {
    output << "\n[PublicBuildingLocations]\nSchema=1\nCount="
        << state.triggers.publicBuildingLocations.size() << '\n';
    int index = 0;
    for (const auto& [id, location] : state.triggers.publicBuildingLocations)
        output << index++ << '=' << id << '|' << runtimeSaveField(location.owner) << '|'
            << location.cell.x << '|' << location.cell.y << '|' << location.noticeTick << '\n';
}

bool restoreRuntimePublicBuildingLocations(RuntimeGameSessionState& state,
    const IniFile& ini, std::string& error) {
    std::map<int, RuntimePublicBuildingLocation> restored;
    if (ini.getSection("PublicBuildingLocations") == nullptr) {
        // An older save already explored its public buildings. Recover their
        // owners quietly instead of replaying a construction alert on loading.
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || object.health <= 0 || object.rubble
                || object.kind != WorldObjectKind::Structure || object.buildAnimTicksRemaining > 0) continue;
            const auto* building = state.rules.findBuildingType(object.type);
            if (building && building->revealToAll)
                restored[object.instanceId] = {runtimeCanonicalHumanHouseOwner(object.owner), {object.x, object.y}, -1};
        }
    } else {
        int schema = 0, count = 0;
        if (!parseRuntimeInt(ini.getValue("PublicBuildingLocations", "Schema", ""), schema) || schema != 1
            || !parseRuntimeInt(ini.getValue("PublicBuildingLocations", "Count", ""), count)
            || count < 0 || count > 100000) { error = "invalid public building location header"; return false; }
        for (int index = 0; index < count; ++index) {
            const auto fields = splitRuntimeSaveFields(ini.getValue("PublicBuildingLocations", std::to_string(index), ""));
            int id = -1; RuntimePublicBuildingLocation location;
            if (fields.size() != 5 || !parseRuntimeInt(fields[0], id) || id < 0
                || fields[1].empty() || !parseRuntimeInt(fields[2], location.cell.x)
                || !parseRuntimeInt(fields[3], location.cell.y)
                || !parseRuntimeInt(fields[4], location.noticeTick) || location.noticeTick < -1
                || location.noticeTick > state.tick || location.cell.x < 0 || location.cell.x >= 512
                || location.cell.y < 0 || location.cell.y >= 512 || restored.contains(id)) {
                error = "invalid public building location record"; return false;
            }
            location.owner = runtimeCanonicalHumanHouseOwner(fields[1]);
            restored.emplace(id, std::move(location));
        }
    }
    state.triggers.publicBuildingLocations = std::move(restored);
    return true;
}

void hashRuntimePublicBuildingLocations(std::uint64_t& hash, const RuntimeGameSessionState& state) {
    runtimeStableHashMixString(hash, "PublicBuildingLocations/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.triggers.publicBuildingLocations.size()));
    for (const auto& [id, location] : state.triggers.publicBuildingLocations) {
        runtimeStableHashMix(hash, id);
        runtimeStableHashMixString(hash, location.owner);
        runtimeStableHashMix(hash, location.cell.x);
        runtimeStableHashMix(hash, location.cell.y);
        runtimeStableHashMix(hash, location.noticeTick);
    }
}

std::string serializeRuntimeWreck(const RuntimeWreckState& wreck, bool canonicalOwner) {
    std::ostringstream out;
    out.imbue(std::locale::classic()); out << std::setprecision(17);
    const auto& object = wreck.object;
    out << object.instanceId << '|' << runtimeSaveField(object.type) << '|'
        << runtimeSaveField(canonicalOwner ? runtimeCanonicalHumanHouseOwner(object.owner) : object.owner)
        << '|' << static_cast<int>(object.kind) << '|'
        << object.facing << '|' << object.turretFacing << '|' << object.authoredAircraft << '|'
        << wreck.startTick << '|' << wreck.sinking << '|' << wreck.jumpjet << '|' << wreck.tilt << '|'
        << wreck.x << '|' << wreck.y << '|' << wreck.startLevel << '|' << wreck.groundLevel << '|'
        << wreck.fallSpeed << '|' << wreck.gravity << '|' << wreck.driftX << '|' << wreck.driftY << '|'
        << runtimeSaveField(wreck.impactAnim) << '|' << runtimeSaveField(wreck.warhead) << '|' << object.subCell;
    return out.str();
}

void writeRuntimeWrecks(const RuntimeGameSessionState& state, std::ostream& output) {
    output << "\n[Wrecks]\nSchema=1\nCount=" << state.wrecks.size() << '\n';
    for (std::size_t i = 0; i < state.wrecks.size(); ++i)
        output << i << '=' << serializeRuntimeWreck(state.wrecks[i]) << '\n';
}

bool restoreRuntimeWrecks(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    std::vector<RuntimeWreckState> restored;
    if (!ini.getSection("Wrecks")) { state.wrecks.clear(); return true; }
    int schema = 0, count = 0;
    const auto fail = [&] { error = "invalid retained wreck state"; return false; };
    if (!parseRuntimeInt(ini.getValue("Wrecks", "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue("Wrecks", "Count", ""), count) || count < 0 || count > 100000) return fail();
    std::unordered_set<int> ids;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue("Wrecks", std::to_string(i), ""));
        if (fields.size() != 22) return fail();
        RuntimeWreckState wreck; auto& object = wreck.object;
        int kind = -1, authored = 0, sinking = 0, jumpjet = 0, tilt = 0;
        if (!parseRuntimeInt(fields[0], object.instanceId) || object.instanceId < 0
            || fields[1].empty() || fields[2].empty() || !ids.insert(object.instanceId).second
            || !parseRuntimeInt(fields[3], kind)
            || (kind != static_cast<int>(WorldObjectKind::Unit) && kind != static_cast<int>(WorldObjectKind::Infantry))
            || !parseRuntimeInt(fields[4], object.facing) || !parseRuntimeInt(fields[5], object.turretFacing)
            || !parseRuntimeInt(fields[6], authored) || !parseRuntimeInt(fields[7], wreck.startTick)
            || wreck.startTick < 0 || wreck.startTick > state.tick
            || !parseRuntimeInt(fields[8], sinking) || !parseRuntimeInt(fields[9], jumpjet)
            || !parseRuntimeInt(fields[10], tilt) || !parseRuntimeInt(fields[21], object.subCell)) return fail();
        const auto number = [&](std::size_t index, double& value) {
            std::istringstream input(fields[index]); input.imbue(std::locale::classic());
            return static_cast<bool>(input >> value) && input.peek() == std::char_traits<char>::eof() && std::isfinite(value);
        };
        if (!number(11, wreck.x) || !number(12, wreck.y) || !number(13, wreck.startLevel)
            || !number(14, wreck.groundLevel) || !number(15, wreck.fallSpeed)
            || !number(16, wreck.gravity) || !number(17, wreck.driftX) || !number(18, wreck.driftY)
            || wreck.x < 0 || wreck.y < 0 || wreck.x >= 512 || wreck.y >= 512
            || wreck.fallSpeed <= 0 || wreck.gravity <= 0
            || std::abs(wreck.driftX) > 1 || std::abs(wreck.driftY) > 1
            || authored < 0 || authored > 1 || sinking < 0 || sinking > 1
            || jumpjet < 0 || jumpjet > 1 || tilt < 0 || tilt > 1) return fail();
        object.kind = static_cast<WorldObjectKind>(kind); object.type = fields[1]; object.owner = fields[2];
        object.authoredAircraft = authored != 0; object.alive = false; object.health = 0;
        wreck.sinking = sinking != 0; wreck.jumpjet = jumpjet != 0; wreck.tilt = tilt != 0;
        wreck.impactAnim = fields[19]; wreck.warhead = fields[20];
        const auto* source = findRuntimeObjectByIdIncludingInactive(state, object.instanceId);
        if (!source || source->alive || source->type != object.type || source->owner != object.owner) return fail();
        restored.push_back(std::move(wreck));
    }
    state.wrecks = std::move(restored); return true;
}

void hashRuntimeWrecks(std::uint64_t& hash, const RuntimeGameSessionState& state) {
    runtimeStableHashMixString(hash, "Wrecks/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.wrecks.size()));
    for (const auto& wreck : state.wrecks) runtimeStableHashMixString(hash, serializeRuntimeWreck(wreck, true));
}

std::string serializeRuntimeBuildingCollapse(const RuntimeBuildingCollapseState& collapse) {
    return std::to_string(collapse.objectId) + '|' + runtimeSaveField(collapse.animId) + '|'
        + std::to_string(collapse.startTick) + '|' + std::to_string(collapse.frameCount) + '|'
        + std::to_string(collapse.rate);
}

void writeRuntimeBuildingCollapses(const RuntimeGameSessionState& state, std::ostream& output) {
    output << "\n[BuildingCollapses]\nSchema=1\nCount=" << state.buildingCollapses.size() << '\n';
    for (std::size_t i = 0; i < state.buildingCollapses.size(); ++i)
        output << i << '=' << serializeRuntimeBuildingCollapse(state.buildingCollapses[i]) << '\n';
}

bool restoreRuntimeBuildingCollapses(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    std::vector<RuntimeBuildingCollapseState> restored;
    if (!ini.getSection("BuildingCollapses")) { state.buildingCollapses.clear(); return true; }
    int schema = 0, count = 0;
    const auto fail = [&] { error = "invalid building collapse state"; return false; };
    if (!parseRuntimeInt(ini.getValue("BuildingCollapses", "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue("BuildingCollapses", "Count", ""), count) || count < 0 || count > 100000) return fail();
    std::unordered_set<int> ids;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue("BuildingCollapses", std::to_string(i), ""));
        RuntimeBuildingCollapseState collapse;
        if (fields.size() != 5 || !parseRuntimeInt(fields[0], collapse.objectId)
            || collapse.objectId < 0 || !ids.insert(collapse.objectId).second || fields[1].empty()
            || !parseRuntimeInt(fields[2], collapse.startTick) || collapse.startTick < 0 || collapse.startTick > state.tick
            || !parseRuntimeInt(fields[3], collapse.frameCount) || collapse.frameCount <= 0 || collapse.frameCount > 100000
            || !parseRuntimeInt(fields[4], collapse.rate) || collapse.rate <= 0 || collapse.rate > 100000) return fail();
        collapse.animId = fields[1];
        const auto* object = findRuntimeObjectByIdIncludingInactive(state, collapse.objectId);
        if (!object || object->alive || object->kind != WorldObjectKind::Structure
            || runtimeBuildingCollapseFrame(collapse, state.tick, state.config.fixedTickHz) >= collapse.frameCount) return fail();
        restored.push_back(std::move(collapse));
    }
    state.buildingCollapses = std::move(restored); return true;
}

void hashRuntimeBuildingCollapses(std::uint64_t& hash, const RuntimeGameSessionState& state) {
    runtimeStableHashMixString(hash, "BuildingCollapses/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.buildingCollapses.size()));
    for (const auto& collapse : state.buildingCollapses)
        runtimeStableHashMixString(hash, serializeRuntimeBuildingCollapse(collapse));
}

bool buildRuntimeOriginalOverlayWalls(const RuntimeGameSessionState& state,
    RuntimeOriginalOverlayWalls& originals, std::string& error) {
    originals.clear();
    std::unordered_map<int, const MapOverlayCell*> pack;
    for (const auto& source : state.world.getOverlayCells())
        pack.emplace(runtimeCellKey(source.x, source.y), &source);
    for (const auto& live : state.world.getTerrainCells()) {
        const auto found = pack.find(runtimeCellKey(live.x, live.y));
        const MapOverlayCell* source = found == pack.end() ? nullptr : found->second;
        if (!source) {
            const auto* fact = runtimeMapQueryCell(state, {live.x, live.y});
            if (fact && fact->hasOverlay) source = &fact->overlay;
        }
        if (!source) continue;
        TerrainCell original = live;
        original.hasOverlay = true;
        original.overlayIndex = source->overlayIndex;
        original.overlayFrame = source->frameIndex;
        original.overlayType = source->type;
        original.overlayImage = source->image;
        original.overlayIsOre = source->isOre;
        original.overlayIsGem = source->isGem;
        initializeWorldOverlayWallState(original, state.rules);
        if (original.overlayWall.health < 0) continue;
        if (live.x < 0 || live.y < 0 || live.x >= 512 || live.y >= 512
            || original.overlayWall.originalFrame < 0
            || original.overlayWall.originalFrame > 255
            || !originals.emplace(runtimeCellKey(live.x, live.y), std::move(original)).second) {
            error = "invalid original map-wall identity";
            return false;
        }
    }
    return true;
}

bool runtimeOverlayWallIdentityMatches(const OverlayWallState& value,
    const OverlayWallState& original) {
    return value.originalIndex == original.originalIndex
        && value.originalFrame == original.originalFrame
        && toUpperAsciiLocal(value.originalType) == toUpperAsciiLocal(original.originalType)
        && toUpperAsciiLocal(value.originalImage) == toUpperAsciiLocal(original.originalImage);
}

bool runtimeTerrainShowsOriginalOverlayWall(const TerrainCell& cell,
    const OverlayWallState& wall) {
    return cell.hasOverlay && cell.overlayIndex == wall.originalIndex
        && (cell.overlayType.empty()
            || toUpperAsciiLocal(cell.overlayType) == toUpperAsciiLocal(wall.originalType));
}

void clearRuntimeOverlayWallSprite(TerrainCell& cell) {
    cell.hasOverlay = false;
    cell.overlayIndex = -1;
    cell.overlayFrame = 0;
    cell.overlayType.clear();
    cell.overlayImage.clear();
    cell.overlayIsOre = cell.overlayIsGem = false;
}

bool resetRuntimeOverlayWallsForSavedProjection(RuntimeGameSessionState& state,
    std::string& error) {
    RuntimeOriginalOverlayWalls originals;
    if (!buildRuntimeOriginalOverlayWalls(state, originals, error)) return false;
    for (auto& cell : state.world.accessTerrainCells()) {
        if (cell.overlayWall.health >= 0
            && runtimeTerrainShowsOriginalOverlayWall(cell, cell.overlayWall))
            clearRuntimeOverlayWallSprite(cell);
        cell.overlayWall = {};
        const auto found = originals.find(runtimeCellKey(cell.x, cell.y));
        if (found == originals.end()) continue;
        const auto& original = found->second;
        cell.overlayWall = original.overlayWall;
        cell.hasOverlay = true;
        cell.overlayIndex = original.overlayIndex;
        cell.overlayFrame = original.overlayFrame;
        cell.overlayType = original.overlayType;
        cell.overlayImage = original.overlayImage;
        cell.overlayIsOre = cell.overlayIsGem = false;
    }
    return true;
}

bool restoreRuntimeOverlayWallStates(RuntimeGameSessionState& state, const IniFile& ini,
    std::string& error) {
    if (!ini.getSection("WallOverlays")) return true;
    const auto fail = [&] { error = "invalid saved map-wall authority"; return false; };
    RuntimeOriginalOverlayWalls originals;
    if (!buildRuntimeOriginalOverlayWalls(state, originals, error)) return false;
    const auto* section = ini.getSection("WallOverlays");
    std::unordered_set<std::string> keys;
    for (const auto& entry : *section)
        if (!keys.insert(toUpperAsciiLocal(trimAsciiLocal(entry.first))).second) return fail();
    int schema = -1, count = -1;
    if (!parseRuntimeInt(ini.getValue("WallOverlays", "Schema", ""), schema) || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue("WallOverlays", "Count", ""), count)
        || count < 0 || static_cast<std::size_t>(count) != originals.size()
        || section->size() != static_cast<std::size_t>(count) + 2U) return fail();
    for (const auto& entry : *section) {
        const auto key = toUpperAsciiLocal(trimAsciiLocal(entry.first));
        if (key == "SCHEMA" || key == "COUNT") continue;
        int index = -1;
        if (!parseRuntimeInt(key, index) || index < 0 || index >= count
            || std::to_string(index) != key) return fail();
    }
    std::unordered_map<int, int> healthByCell;
    std::unordered_map<int, int> currentFrameByCell;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue("WallOverlays", std::to_string(i), ""));
        int x = -1, y = -1, ordinal = -1, frame = -1, health = -1, currentFrame = -1;
        if (fields.size() != (schema == 1 ? 7U : 8U)
            || !parseRuntimeInt(fields[0], x) || !parseRuntimeInt(fields[1], y)
            || x < 0 || y < 0 || x >= 512 || y >= 512
            || !parseRuntimeInt(fields[2], ordinal) || !parseRuntimeInt(fields[5], frame)
            || !parseRuntimeInt(fields[6], health)
            || (schema == 2 && (!parseRuntimeInt(fields[7], currentFrame)
                || (health == 0 ? currentFrame != -1 : (currentFrame < 0 || currentFrame > 255))))) return fail();
        const auto original = originals.find(runtimeCellKey(x, y));
        if (original == originals.end()) return fail();
        const auto& wall = original->second.overlayWall;
        const auto* building = state.rules.findBuildingType(wall.originalType);
        if (ordinal != wall.originalIndex || frame != wall.originalFrame
            || toUpperAsciiLocal(fields[3]) != toUpperAsciiLocal(wall.originalType)
            || toUpperAsciiLocal(fields[4]) != toUpperAsciiLocal(wall.originalImage)
            || !building || !building->wall || building->strength <= 0
            || health < 0 || health > building->strength
            || !healthByCell.emplace(runtimeCellKey(x, y), health).second) return fail();
        if (schema == 2) currentFrameByCell.emplace(runtimeCellKey(x, y), currentFrame);
    }
    // All rows and schema-specific frames were validated against immutable
    // identity before applying any HP or current artwork.
    for (auto& cell : state.world.accessTerrainCells()) {
        const auto saved = healthByCell.find(runtimeCellKey(cell.x, cell.y));
        if (saved == healthByCell.end()) continue;
        const auto& original = originals.at(saved->first);
        if (saved->second > 0 && cell.hasOverlay
            && !runtimeTerrainShowsOriginalOverlayWall(cell, original.overlayWall)) return fail();
    }
    for (auto& cell : state.world.accessTerrainCells()) {
        const auto saved = healthByCell.find(runtimeCellKey(cell.x, cell.y));
        if (saved == healthByCell.end()) continue;
        const auto& original = originals.at(saved->first);
        cell.overlayWall = original.overlayWall;
        cell.overlayWall.health = saved->second;
        if (saved->second == 0) {
            if (runtimeTerrainShowsOriginalOverlayWall(cell, cell.overlayWall))
                clearRuntimeOverlayWallSprite(cell);
        } else {
            cell.hasOverlay = true;
            cell.overlayIndex = cell.overlayWall.originalIndex;
            cell.overlayType = cell.overlayWall.originalType;
            cell.overlayImage = cell.overlayWall.originalImage;
            cell.overlayIsOre = cell.overlayIsGem = false;
            if (schema == 2) cell.overlayFrame = currentFrameByCell.at(saved->first);
        }
    }
    // Schema1 retains its established reconstruction. Schema2 records current
    // artwork exactly; loading must not rewrite saved visible pixels.
    if (schema == 1) refreshRuntimeOverlayWallTopology(state);
    return true;
}

bool writeRuntimeOverlayWallStates(const RuntimeGameSessionState& state,
    std::ostream& output, std::string& error) {
    RuntimeOriginalOverlayWalls originals;
    if (!buildRuntimeOriginalOverlayWalls(state, originals, error)) return false;
    std::map<int, const TerrainCell*> current;
    for (const auto& cell : state.world.getTerrainCells()) {
        if (cell.overlayWall.health < 0) continue;
        if (!originals.contains(runtimeCellKey(cell.x, cell.y))
            || !current.emplace(runtimeCellKey(cell.x, cell.y), &cell).second) {
            error = "map-wall save lacks original identity";
            return false;
        }
    }
    if (current.size() != originals.size()) {
        error = "map-wall save is missing initialized wall authority";
        return false;
    }
    for (const auto& [key, original] : originals) {
        const auto& cell = *current.at(key);
        const auto* building = state.rules.findBuildingType(original.overlayWall.originalType);
        if (!runtimeOverlayWallIdentityMatches(cell.overlayWall, original.overlayWall)
            || !building || !building->wall || building->strength <= 0
            || cell.overlayWall.health < 0 || cell.overlayWall.health > building->strength
            || (cell.overlayWall.health > 0
                && (!runtimeTerrainShowsOriginalOverlayWall(cell, cell.overlayWall)
                    || cell.overlayFrame < 0 || cell.overlayFrame > 255))
            || (cell.overlayWall.health == 0
                && runtimeTerrainShowsOriginalOverlayWall(cell, cell.overlayWall))) {
            error = "invalid live map-wall save projection";
            return false;
        }
    }
    output << "\n[WallOverlays]\nSchema=2\nCount=" << current.size() << '\n';
    std::size_t index = 0;
    for (const auto& [key, ptr] : current) {
        (void)key;
        const auto& cell = *ptr;
        const auto& wall = cell.overlayWall;
        output << index++ << '=' << cell.x << '|' << cell.y << '|'
            << wall.originalIndex << '|' << runtimeSaveField(wall.originalType) << '|'
            << runtimeSaveField(wall.originalImage) << '|' << wall.originalFrame << '|'
            << wall.health << '|' << (wall.health > 0 ? cell.overlayFrame : -1) << '\n';
    }
    return true;
}

void writeRuntimeVirusDeaths(const RuntimeGameSessionState& state, std::ostream& output) {
    const auto count = std::count_if(state.impactEffects.begin(), state.impactEffects.end(),
        [](const auto& impact) { return impact.lifecycleState == "VirusDeath"; });
    output << "\n[VirusDeathAnimations]\nSchema=1\nCount=" << count << '\n';
    int index = 0;
    for (const auto& impact : state.impactEffects) {
        if (impact.lifecycleState == "VirusDeath")
            output << index++ << '=' << runtimeMultiplayerSnapshotImpactRecord(impact) << '\n';
    }
}

bool loadRuntimeVirusDeaths(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    const std::string schema = ini.getValue("VirusDeathAnimations", "Schema", "");
    if (schema.empty()) return true; // Legacy saves had no pending virus deaths.
    int count = 0;
    if (schema != "1" || !parseRuntimeInt(ini.getValue("VirusDeathAnimations", "Count", ""), count)
        || count < 0 || count > 65536) {
        error = "invalid VirusDeathAnimations section";
        return false;
    }
    std::vector<RuntimeImpactEffect> restored;
    for (int index = 0; index < count; ++index) {
        RuntimeImpactEffect impact;
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(
                ini.getValue("VirusDeathAnimations", std::to_string(index), ""), impact)
            || impact.lifecycleState != "VirusDeath" || !impact.death
            || impact.deferredParticleAnimId.empty()) {
            error = "invalid pending virus death animation";
            return false;
        }
        restored.push_back(std::move(impact));
    }
    state.impactEffects = std::move(restored);
    return true;
}

bool runtimePersistentWreckWaterAnimation(const RuntimeImpactEffect& impact) {
    return !impact.death && impact.remainingTicks > 0
        && (impact.lifecycleState == "WreckWaterEntry" || impact.lifecycleState == "WreckWake"
            || impact.lifecycleState == "WreckWaterImpact");
}

bool runtimePersistentUnitDeath(const RuntimeImpactEffect& impact) {
    return (impact.death && impact.remainingTicks > 0
        && (impact.lifecycleState == "UnitDestruction" || impact.lifecycleState == "InfantryDeath"))
        || runtimePersistentWreckWaterAnimation(impact);
}

bool runtimeValidWreckWaterAnimation(const RuntimeGameSessionState& state, const RuntimeImpactEffect& impact) {
    if (!runtimePersistentWreckWaterAnimation(impact) || impact.renderIntent != "waterWake"
        || !impact.hasWorldPosition || !std::isfinite(impact.worldX)
        || !std::isfinite(impact.worldY) || !std::isfinite(impact.worldLevel)
        || impact.damage != 0 || impact.cellSpread != 0.0
        || impact.warheadImpactIntentId != impact.animId) return false;
    return impact.lifecycleState == "WreckWake"
        ? impact.animId == state.rules.getWake() && !impact.animId.empty()
        : impact.animId == "H2O_EXP3";
}

void writeRuntimeUnitDeaths(const RuntimeGameSessionState& state, std::ostream& output) {
    const auto count = std::count_if(state.impactEffects.begin(), state.impactEffects.end(), runtimePersistentUnitDeath);
    output << "\n[UnitDeathAnimations]\nSchema=1\nCount=" << count << '\n';
    int index = 0;
    for (const auto& impact : state.impactEffects) {
        if (!runtimePersistentUnitDeath(impact)) continue;
        output << "Impact" << index << '=' << runtimeMultiplayerSnapshotImpactRecord(impact) << '\n';
        output << "Presentation" << index++ << '=' << iniHexEncode(impact.animId) << '|'
            << iniHexEncode(impact.renderIntent) << '|' << impact.frameIndex << '|'
            << impact.facing << '|' << impact.radius << '|' << runtimeBoolText(impact.bright) << '\n';
    }
}

bool loadRuntimeUnitDeaths(RuntimeGameSessionState& state, const IniFile& ini, std::string& error, bool commit) {
    const std::string schema = ini.getValue("UnitDeathAnimations", "Schema", "");
    if (!ini.getSection("UnitDeathAnimations")) return true; // Earlier saves omitted ordinary corpses.
    int count = 0;
    if (schema != "1" || !parseRuntimeInt(ini.getValue("UnitDeathAnimations", "Count", ""), count)
        || count < 0 || count > 65536) {
        error = "invalid UnitDeathAnimations section"; return false;
    }
    std::vector<RuntimeImpactEffect> restored;
    restored.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        RuntimeImpactEffect impact;
        const auto presentation = splitRuntimeSaveFields(
            ini.getValue("UnitDeathAnimations", "Presentation" + std::to_string(index), ""));
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(
                ini.getValue("UnitDeathAnimations", "Impact" + std::to_string(index), ""), impact)
            || !runtimePersistentUnitDeath(impact) || presentation.size() != 6
            || !iniHexDecode(presentation[0], impact.animId, 256U)
            || !iniHexDecode(presentation[1], impact.renderIntent, 256U)
            || !parseRuntimeInt(presentation[2], impact.frameIndex)
            || !parseRuntimeInt(presentation[3], impact.facing)
            || !parseRuntimeInt(presentation[4], impact.radius)
            || !parseRuntimeStrictBool(presentation[5], impact.bright)
            || impact.facing < -1 || impact.facing > 255 || impact.radius < 0 || impact.radius > 4096
            || impact.frameIndex < -1 || impact.frameIndex > 1000000
            || (runtimePersistentWreckWaterAnimation(impact)
                ? !runtimeValidWreckWaterAnimation(state, impact)
                : (impact.renderIntent != "unitDestruction" && impact.renderIntent != "infantryDeath"))) {
            error = "invalid unit death animation"; return false;
        }
        restored.push_back(std::move(impact));
    }
    if (commit) for (auto& impact : restored) state.impactEffects.push_back(std::move(impact));
    return true;
}

void writeRuntimeSquidSplashes(const RuntimeGameSessionState& state, std::ostream& output) {
    const auto count=std::count_if(state.impactEffects.begin(),state.impactEffects.end(),
        [](const auto& impact){return impact.lifecycleState=="SquidSplash" && impact.remainingTicks>0;});
    output<<"\n[SquidSplashAnimations]\nSchema=1\nCount="<<count<<'\n';
    int index=0;
    for (const auto& impact:state.impactEffects) {
        if (impact.lifecycleState=="SquidSplash" && impact.remainingTicks>0)
            output<<index++<<'='<<runtimeMultiplayerSnapshotImpactRecord(impact)<<'\n';
    }
}

bool loadRuntimeSquidSplashes(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    const auto schema=ini.getValue("SquidSplashAnimations","Schema","");
    if (schema.empty()) return true;
    int count=0;
    if (schema!="1" || !parseRuntimeInt(ini.getValue("SquidSplashAnimations","Count",""),count)
        || count<0 || count>65536) { error="invalid SquidSplashAnimations section"; return false; }
    std::vector<RuntimeImpactEffect> restored;
    for (int index=0;index<count;++index) {
        RuntimeImpactEffect impact;
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(
                ini.getValue("SquidSplashAnimations",std::to_string(index),""),impact)
            || impact.lifecycleState!="SquidSplash" || impact.remainingTicks<=0
            || impact.death || impact.damage!=0 || !impact.warheadId.empty()
            || !impact.hasWorldPosition || !std::isfinite(impact.worldX) || !std::isfinite(impact.worldY)
            || !std::isfinite(impact.worldLevel) || impact.warheadImpactIntentId.empty()
            || impact.animationSequenceSteps<=0 || impact.animationFrameCount<=0) {
            error="invalid squid splash animation"; return false;
        }
        impact.animId=impact.warheadImpactIntentId;
        impact.renderIntent="squidSplash";
        impact.frameIndex=runtimeImpactAnimationFrameForStep(impact,
            (impact.totalTicks-impact.remainingTicks)/impact.animationFrameDelayTicks);
        restored.push_back(std::move(impact));
    }
    for (auto& impact:restored) state.impactEffects.push_back(std::move(impact));
    return true;
}

void writeRuntimeSpriteFirings(const RuntimeGameSessionState& state, std::ostream& output) {
    std::vector<const WorldObjectInstance*> active;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.health <= 0 || object.spriteFiringStartTick < 0) continue;
        const auto* art = runtimeArtTypeForObject(state, object);
        if (art && art->firingFrames > 0 && runtimeRuleFrameAtTick(
                state.tick - object.spriteFiringStartTick, state.config.fixedTickHz) < 2LL * art->firingFrames)
            active.push_back(&object);
    }
    std::sort(active.begin(), active.end(), [](const auto* a, const auto* b) {
        return a->instanceId < b->instanceId;
    });
    output << "\n[SpriteFiringPresentation]\nSchema=1\nCount=" << active.size() << '\n';
    for (std::size_t i = 0; i < active.size(); ++i)
        output << i << '=' << active[i]->instanceId << '|' << active[i]->spriteFiringStartTick << '\n';
}

bool loadRuntimeSpriteFirings(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    constexpr const char* section = "SpriteFiringPresentation";
    if (ini.getSection(section) == nullptr) return true;
    const auto fail = [&]() { error = "invalid sprite firing presentation"; return false; };
    int schema = 0, count = -1;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size()) return fail();
    std::vector<std::pair<WorldObjectInstance*, int>> pending;
    std::unordered_set<int> seen;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        int id = -1, start = -1;
        if (fields.size() != 2 || !parseRuntimeInt(fields[0], id) || !seen.insert(id).second
            || !parseRuntimeInt(fields[1], start) || start < 0 || start > state.tick) return fail();
        auto* object = findMutableObjectById(state, id);
        if (!object || !object->alive || object->health <= 0 || object->kind != WorldObjectKind::Unit)
            return fail();
        const auto* art = runtimeArtTypeForObject(state, *object);
        if (!art || art->voxel || art->firingFrames <= 0 || runtimeRuleFrameAtTick(
                state.tick - start, state.config.fixedTickHz) >= 2LL * art->firingFrames) return fail();
        pending.emplace_back(object, start);
    }
    for (auto& object : state.world.accessObjects()) object.spriteFiringStartTick = -1;
    for (const auto& [object, start] : pending) object->spriteFiringStartTick = start;
    return true;
}

bool runtimeHasSupportFlightPose(const WorldObjectInstance& object) {
    return object.alive && object.airborne && (object.mission=="ParadropDrop"
        || object.mission=="SupportFlightEast" || object.mission=="SupportFlightWest");
}

void writeRuntimeSupportFlightPoses(const RuntimeGameSessionState& state, std::ostream& output) {
    const auto precision=output.precision(); output<<std::setprecision(17);
    const auto& objects=state.world.getObjects();
    output<<"\n[SupportFlightPoses]\nVersion=1\nCount="
        <<std::count_if(objects.begin(),objects.end(),runtimeHasSupportFlightPose)<<"\n";
    int index=0;
    for (const auto& object:objects) if (runtimeHasSupportFlightPose(object)) {
        output<<index++<<"="<<object.instanceId<<"|"<<object.renderX<<"|"<<object.renderY
            <<"|"<<object.renderLevel<<"|"<<object.movementAnimationDistance<<"\n";
    }
    output.precision(precision);
}

bool restoreRuntimeSupportFlightPoses(const IniFile& ini,
    std::vector<WorldObjectInstance>& objects, std::string& error) {
    const auto reject=[&]() {error="invalid saved support aircraft/paratrooper pose";return false;};
    if (!ini.getSection("SupportFlightPoses")) {
        // Earlier saves retain integer world coordinates but no sub-cell
        // pose. Resume those flights from that last known position.
        for (auto& object:objects) if (runtimeHasSupportFlightPose(object)) {
            object.hasRenderPosition=object.renderMoveActive=true;
            object.renderX=object.x; object.renderY=object.y; object.renderLevel=object.level;
        }
        return true;
    }
    int version=0,count=-1;
    const auto expected=std::count_if(objects.begin(),objects.end(),runtimeHasSupportFlightPose);
    if (!parseRuntimeInt(ini.getValue("SupportFlightPoses","Version",""),version) || version!=1
        || !parseRuntimeInt(ini.getValue("SupportFlightPoses","Count",""),count)
        || count!=expected) return reject();
    std::unordered_set<int> restored;
    for (int i=0;i<count;++i) {
        const auto fields=splitRuntimeSaveFields(ini.getValue("SupportFlightPoses",std::to_string(i),""));
        int id=-1; double x=0,y=0,z=0,distance=0;
        if (fields.size()!=5 || !parseRuntimeInt(fields[0],id) || !restored.insert(id).second
            || !parseRuntimeFiniteDouble(fields[1],x) || !parseRuntimeFiniteDouble(fields[2],y)
            || !parseRuntimeFiniteDouble(fields[3],z) || !parseRuntimeFiniteDouble(fields[4],distance)
            || distance<0.0 || distance>1e9) return reject();
        const auto object=std::find_if(objects.begin(),objects.end(),[&](const auto& candidate){return candidate.instanceId==id;});
        if (object==objects.end() || !runtimeHasSupportFlightPose(*object)
            || std::abs(x-object->x)>0.500001 || std::abs(y-object->y)>0.500001
            || std::abs(z-object->level)>0.500001 || object->flightLevel!=object->level) return reject();
        object->hasRenderPosition=object->renderMoveActive=true;
        object->renderX=x; object->renderY=y; object->renderLevel=z;
        object->movementAnimationDistance=distance;
    }
    return true;
}

void writeRuntimeFireReveals(const RuntimeGameSessionState& state, std::ostream& output) {
    output << "\n[FireReveals]\nSchema=1\nDataHex="
        << iniHexEncode(state.visionEnabled ? state.vision.serializeFireReveals(state.tick) : "1 0\n") << '\n';
}

bool readRuntimeFireReveals(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    if (!ini.getSection("FireReveals")) return true;
    std::string data;
    if (ini.getValue("FireReveals", "Schema", "") != "1"
        || !iniHexDecode(ini.getValue("FireReveals", "DataHex", ""), data, 4U*1024U*1024U)
        || !state.vision.restoreFireReveals(data, state.tick, state.config.fixedTickHz)) {
        error = "invalid weapon fire reveal save section";
        return false;
    }
    return true;
}

RuntimeGarrisonConfig runtimeSavedGarrisonConfig(
    const RuntimeGameSessionState& state) {
    RuntimeGarrisonConfig config;
    if (state.rulesLoaded) {
        const SpecialMechanismRules& rules =
            state.rules.getSpecialMechanismRules();
        config.conditionRedFraction = rules.conditionRed;
        config.occupyDamageMultiplier = rules.occupyDamageMultiplier;
        config.occupyRofMultiplier = rules.occupyRofMultiplier;
        config.occupyWeaponRange = static_cast<double>(
            rules.occupyWeaponRange
        );
    }
    config.fixedTickHz = std::max(1, state.config.fixedTickHz);
    return config;
}

bool parseRuntimeSavedAirstrikeHost(
    const IniFile& ini,
    int saveVersion,
    RuntimeSavedAirstrikeHostCandidate& candidate,
    std::string& errorMessage) {
    candidate = RuntimeSavedAirstrikeHostCandidate{};
    if (saveVersion < 13) {
        return true;
    }
    if (!candidate.airstrikes.load(ini, &errorMessage)) {
        return false;
    }
    const int hostSchema = parseRuntimeIntOrDefault(
        ini.getValue("RuntimeAirstrikeHost", "Schema", "-1"), -1);
    if (hostSchema != 1 && hostSchema != 2) {
        errorMessage = "unsupported AirstrikeHost schema"; return false;
    }
    int bindingCount = -1;
    int authorityCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("RuntimeAirstrikeHost", "BindingCount", ""),
            bindingCount
        )
        || !parseRuntimeInt(
            ini.getValue(
                "RuntimeAirstrikeHost",
                "RootAuthorityCount",
                ""
            ),
            authorityCount
        )
        || bindingCount < 0 || authorityCount < 0
        || static_cast<std::size_t>(bindingCount)
            > candidate.airstrikes.limits().maxTotalPlanes
        || static_cast<std::size_t>(authorityCount)
            > candidate.airstrikes.limits().maxProjectileReceipts) {
        errorMessage = "invalid AirstrikeHost header";
        return false;
    }
    const auto validText = [](const std::string& value) {
        const std::string trimmed = trimAsciiLocal(value);
        return !trimmed.empty() && trimmed.size() <= 128U;
    };
    for (int index = 0; index < bindingCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue(
                "RuntimeAirstrikeHost",
                "Binding" + std::to_string(index),
                ""
            )
        );
        RuntimeAirstrikePlaneBinding binding;
        bool elite = false;
        if (tokens.size() != 20U
            || !parseRuntimeInt(tokens[0], binding.planeToken)
            || !parseRuntimeInt(tokens[1], binding.strikeId)
            || !parseRuntimeInt(tokens[2], binding.objectId)
            || !parseRuntimeInt(tokens[3], binding.callerObjectId)
            || !parseRuntimeInt(tokens[4], binding.targetObjectId)
            || !parseRuntimeInt(tokens[5], binding.spawnTick)
            || !parseRuntimeInt(tokens[6], binding.bombTick)
            || !parseRuntimeInt(tokens[7], binding.egressStartTick)
            || !parseRuntimeInt(tokens[8], binding.despawnTick)
            || !parseRuntimeStrictBool(tokens[9], elite)
            || !parseRuntimeInt(tokens[14], binding.route.entry.x)
            || !parseRuntimeInt(tokens[15], binding.route.entry.y)
            || !parseRuntimeInt(tokens[16], binding.route.target.x)
            || !parseRuntimeInt(tokens[17], binding.route.target.y)
            || !parseRuntimeInt(tokens[18], binding.route.exit.x)
            || !parseRuntimeInt(tokens[19], binding.route.exit.y)) {
            errorMessage = "invalid AirstrikeHost binding record";
            return false;
        }
        binding.elite = elite;
        binding.owner = trimAsciiLocal(tokens[10]);
        binding.aircraftTypeId = trimAsciiLocal(tokens[11]);
        binding.weaponId = trimAsciiLocal(tokens[12]);
        binding.warheadId = trimAsciiLocal(tokens[13]);
        if (binding.planeToken <= 0 || binding.strikeId <= 0
            || binding.objectId <= 0 || binding.callerObjectId <= 0
            || binding.targetObjectId <= 0
            || binding.spawnTick < 0
            || binding.bombTick < binding.spawnTick
            || binding.egressStartTick < binding.spawnTick
            || binding.egressStartTick > binding.despawnTick
            || binding.despawnTick < binding.spawnTick
            || !validText(binding.owner)
            || !validText(binding.aircraftTypeId)
            || !validText(binding.weaponId)
            || !validText(binding.warheadId)
            || !candidate.bindingsByToken.emplace(
                binding.planeToken,
                binding
            ).second) {
            errorMessage = "invalid or duplicate AirstrikeHost binding";
            return false;
        }
    }
    if(ini.getSection("RuntimeAirstrikeEgressOrigins")!=nullptr) {
        int version=0,count=-1;
        if(!parseRuntimeInt(ini.getValue("RuntimeAirstrikeEgressOrigins","Version",""),version)||version!=1
            ||!parseRuntimeInt(ini.getValue("RuntimeAirstrikeEgressOrigins","Count",""),count)
            ||count<0||count>bindingCount) {errorMessage="invalid exact Airstrike egress header";return false;}
        std::set<int> restored;
        for(int index=0;index<count;++index) {
            const auto fields=splitRuntimeSaveFields(ini.getValue("RuntimeAirstrikeEgressOrigins",std::to_string(index),""));
            int token=-1;double x=0,y=0;
            if(fields.size()!=3||!parseRuntimeInt(fields[0],token)||!restored.insert(token).second
                ||!parseRuntimeFiniteDouble(fields[1],x)||!parseRuntimeFiniteDouble(fields[2],y)) {
                errorMessage="invalid exact Airstrike egress record";return false;
            }
            const auto binding=candidate.bindingsByToken.find(token);
            if(binding==candidate.bindingsByToken.end()) {errorMessage="unbound exact Airstrike egress origin";return false;}
            binding->second.hasPreciseEgressOrigin=true;
            binding->second.preciseEgressOriginX=x;binding->second.preciseEgressOriginY=y;
        }
    }
    for (int index = 0; index < authorityCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue(
                "RuntimeAirstrikeHost",
                "Root" + std::to_string(index),
                ""
            )
        );
        RuntimeAirstrikeRootAuthority authority;
        bool elite = false;
        if (tokens.size() != 18U
            || !parseRuntimeInt(
                tokens[0],
                authority.rootProjectileActorId
            )
            || !parseRuntimeInt(tokens[1], authority.strikeId)
            || !parseRuntimeInt(tokens[2], authority.planeToken)
            || !parseRuntimeInt(tokens[3], authority.planeObjectId)
            || !parseRuntimeInt(tokens[4], authority.callerObjectId)
            || !parseRuntimeInt(tokens[5], authority.targetObjectId)
            || !parseRuntimeInt(tokens[6], authority.launchTick)
            || !parseRuntimeInt(tokens[7], authority.terminalTick)
            || !parseRuntimeStrictBool(tokens[8], elite)
            || !parseRuntimeInt(tokens[9], authority.bombCell.x)
            || !parseRuntimeInt(tokens[10], authority.bombCell.y)
            || !parseRuntimeInt(tokens[16], authority.damage)
            || !parseRuntimeFiniteDouble(
                tokens[17],
                authority.veteranDamageMultiplier
            )) {
            errorMessage = "invalid AirstrikeHost root authority record";
            return false;
        }
        authority.elite = elite;
        authority.owner = trimAsciiLocal(tokens[11]);
        authority.aircraftTypeId = trimAsciiLocal(tokens[12]);
        authority.weaponId = trimAsciiLocal(tokens[13]);
        authority.warheadId = trimAsciiLocal(tokens[14]);
        authority.projectileId = trimAsciiLocal(tokens[15]);
        if (hostSchema == 1) {
            // Legacy saves already fired their one missile; never invent an
            // additional round when resuming an older simulation.
            authority.rounds.push_back({authority.rootProjectileActorId,
                authority.launchTick, authority.terminalTick, authority.bombCell, authority.damage});
        } else {
            const auto salvo = splitRuntimeSaveFields(ini.getValue("RuntimeAirstrikeHost",
                "Salvo" + std::to_string(index), ""));
            int count = 0;
            if (salvo.size() < 3 || !parseRuntimeInt(salvo[0], authority.roundsRemaining)
                || !parseRuntimeInt(salvo[1], authority.nextFireTick)
                || !parseRuntimeInt(salvo[2], count) || count < 1 || count > 256
                || salvo.size() != 3U + 6U * static_cast<std::size_t>(count)
                || authority.roundsRemaining < 0 || authority.roundsRemaining > 256 - count
                || (authority.roundsRemaining == 0 ? authority.nextFireTick != -1
                    : authority.nextFireTick < authority.launchTick)) {
                errorMessage = "invalid AirstrikeHost salvo header"; return false;
            }
            for (int shot = 0; shot < count; ++shot) {
                const auto offset = 3U + 6U * static_cast<std::size_t>(shot);
                RuntimeAirstrikeSalvoRound round;
                if (!parseRuntimeInt(salvo[offset], round.actorId)
                    || !parseRuntimeInt(salvo[offset+1], round.launchTick)
                    || !parseRuntimeInt(salvo[offset+2], round.terminalTick)
                    || !parseRuntimeInt(salvo[offset+3], round.sourceCell.x)
                    || !parseRuntimeInt(salvo[offset+4], round.sourceCell.y)
                    || !parseRuntimeInt(salvo[offset+5], round.damage)
                    || round.actorId <= 0 || round.launchTick < authority.launchTick
                    || round.terminalTick < -1
                    || (round.terminalTick >= 0 && round.terminalTick < round.launchTick)
                    || (shot > 0 && (round.actorId <= authority.rounds.back().actorId
                        || round.launchTick < authority.rounds.back().launchTick))) {
                    errorMessage = "invalid AirstrikeHost salvo round"; return false;
                }
                authority.rounds.push_back(round);
            }
            const auto& first = authority.rounds.front();
            if (first.actorId != authority.rootProjectileActorId || first.launchTick != authority.launchTick
                || first.sourceCell.x != authority.bombCell.x || first.sourceCell.y != authority.bombCell.y
                || first.damage != authority.damage) {
                errorMessage = "AirstrikeHost first round identity mismatch"; return false;
            }
        }
        if (authority.rootProjectileActorId <= 0
            || authority.strikeId <= 0 || authority.planeToken <= 0
            || authority.planeObjectId <= 0
            || authority.callerObjectId <= 0
            || authority.targetObjectId <= 0
            || authority.launchTick < 0
            || (authority.terminalTick >= 0
                && authority.terminalTick < authority.launchTick)
            || authority.terminalTick < -1
            || !validText(authority.owner)
            || !validText(authority.aircraftTypeId)
            || !validText(authority.weaponId)
            || !validText(authority.warheadId)
            || !validText(authority.projectileId)
            || !std::isfinite(authority.veteranDamageMultiplier)
            || authority.veteranDamageMultiplier < 0.0
            || authority.veteranDamageMultiplier > 1000.0
            || !candidate.authoritiesByActor.emplace(
                authority.rootProjectileActorId,
                authority
            ).second) {
            errorMessage =
                "invalid or duplicate AirstrikeHost root authority";
            return false;
        }
    }
    return true;
}

double runtimeWarheadPercentAtMaxFraction(const WarheadType& warhead) {
    if (warhead.percentAtMax <= 0.0) {
        return 1.0;
    }
    if (warhead.percentAtMax > 1.0) {
        return std::clamp(warhead.percentAtMax / 100.0, 0.0, 1.0);
    }
    return std::clamp(warhead.percentAtMax, 0.0, 1.0);
}

int runtimeOreBailsForWarheadImpact(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain,
    int damage,
    const WarheadType& warhead,
    double distance,
    const std::string& impactAnimId) {
    const int maxImpactBails = (runtimeOreMaxBails + 1) / 2;
    if (toUpperAsciiLocal(impactAnimId).rfind("S_CLSN", 0) == 0) {
        return maxImpactBails;
    }

    const int bailValue = std::max(1, runtimeResourceValueForCell(state, terrain));
    double scaledDamage = static_cast<double>(std::max(1, damage));
    if (warhead.cellSpread > 0.0) {
        const double radius = std::max(0.1, warhead.cellSpread);
        const double t = std::clamp(distance / radius, 0.0, 1.0);
        const double percentAtMax = runtimeWarheadPercentAtMaxFraction(warhead);
        scaledDamage *= 1.0 - ((1.0 - percentAtMax) * t);
    }

    return std::clamp(
        static_cast<int>(std::ceil(scaledDamage / static_cast<double>(bailValue))),
        1,
        maxImpactBails
    );
}

RuntimeOreDamageResult applyRuntimeTiberiumWarheadOreDamage(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& impactCell,
    int damage,
    double cellSpread,
    const std::string& impactAnimId,
    const WarheadType* warhead) {
    RuntimeOreDamageResult result;
    if (warhead == nullptr || !warhead->tiberium) {
        return result;
    }

    const double radius = std::max(0.0, cellSpread);
    std::vector<RuntimeCellPoint> cellsToDamage;
    cellsToDamage.reserve(radius > 0.0 ? 16 : 1);
    for (const TerrainCell& terrain : state.world.getTerrainCells()) {
        if (!terrain.hasOverlay
            || (!terrain.overlayIsOre && !terrain.overlayIsGem)) {
            continue;
        }

        const RuntimeCellPoint cell{terrain.x, terrain.y};
        const double distance = runtimeCellDistance(impactCell, cell);
        if (cell == impactCell || (radius > 0.0 && distance <= radius)) {
            cellsToDamage.push_back(cell);
        }
    }

    for (const RuntimeCellPoint& cell : cellsToDamage) {
        const TerrainCell* terrain = findRuntimeTerrainCellAt(state, cell);
        if (terrain == nullptr) {
            continue;
        }

        const double distance = runtimeCellDistance(impactCell, cell);
        const int bailsToRemove = runtimeOreBailsForWarheadImpact(
            state,
            *terrain,
            damage,
            *warhead,
            distance,
            impactAnimId
        );
        const int removed =
            removeRuntimeOreBailsAtCell(state, cell, bailsToRemove);
        if (removed <= 0) {
            continue;
        }
        ++result.cellsTouched;
        result.bailsRemoved += removed;
    }
    result.changed = result.bailsRemoved > 0;
    return result;
}

void runtimeMultiplayerAuthoritativeMixImpact(
    std::uint64_t& hash,
    const RuntimeImpactEffect& impact) {
    runtimeStableHashMix(hash, impact.projectileActorId);
    runtimeStableHashMix(hash, impact.attackerObjectId);
    runtimeStableHashMix(hash, impact.targetObjectId);
    runtimeMultiplayerAuthoritativeMixBool(hash, impact.targetIsStructure);
    runtimeStableHashMixString(
        hash,
        runtimeCanonicalHumanHouseOwner(impact.owner)
    );
    runtimeMultiplayerAuthoritativeMixCell(hash, impact.cell);
    runtimeStableHashMix(hash, impact.level);
    runtimeMultiplayerAuthoritativeMixBool(hash, impact.hasWorldPosition);
    runtimeMultiplayerAuthoritativeMixDouble(hash, impact.worldX);
    runtimeMultiplayerAuthoritativeMixDouble(hash, impact.worldY);
    runtimeMultiplayerAuthoritativeMixDouble(hash, impact.worldLevel);
    runtimeStableHashMix(hash, impact.totalTicks);
    runtimeStableHashMix(hash, impact.remainingTicks);
    // Authored animation sequencing is simulation-bearing because its
    // midpoint can create ore/smudges. Screen offsets, facing, frame cache,
    // visual radius and image/intent identifiers are presentation-only.
    runtimeMultiplayerAuthoritativeMixDouble(hash, impact.muzzleAnchorX);
    runtimeMultiplayerAuthoritativeMixDouble(hash, impact.muzzleAnchorY);
    runtimeMultiplayerAuthoritativeMixDouble(hash, impact.muzzleAnchorLevel);
    runtimeStableHashMix(hash, impact.muzzleFacing);
    runtimeStableHashMix(hash, impact.animationFrameCount);
    runtimeStableHashMix(hash, impact.animationFrameDelayTicks);
    runtimeStableHashMix(hash, impact.animationStartFrame);
    runtimeStableHashMix(hash, impact.animationLoopStartFrame);
    runtimeStableHashMix(hash, impact.animationLoopEndFrame);
    runtimeStableHashMix(hash, impact.animationLoopCount);
    runtimeStableHashMix(hash, impact.animationEndFrame);
    runtimeStableHashMix(hash, impact.animationSequenceSteps);
    runtimeStableHashMix(hash, impact.animationMidpointWidth);
    runtimeStableHashMix(hash, impact.animationMidpointHeight);
    runtimeStableHashMix(hash, impact.lastSmudgeMidpointStep);
    runtimeStableHashMixString(hash, runtimeMultiplayerCanonicalIdentifier(impact.deferredParticleAnimId));
    runtimeStableHashMixString(hash, runtimeCanonicalHumanHouseOwner(impact.deferredParticleOwner));
    runtimeStableHashMix(hash, impact.deferredParticleTicks);
    runtimeMultiplayerAuthoritativeMixBool(hash, impact.death);
    runtimeStableHashMixString(hash, impact.lifecycleState);
    runtimeStableHashMixString(
        hash,
        runtimeMultiplayerCanonicalIdentifier(impact.weaponId)
    );
    runtimeStableHashMixString(hash, impact.weaponSlot);
    runtimeStableHashMixString(
        hash,
        runtimeMultiplayerCanonicalIdentifier(impact.projectileId)
    );
    runtimeStableHashMixString(
        hash,
        runtimeMultiplayerCanonicalIdentifier(impact.warheadId)
    );
    runtimeStableHashMixString(hash, impact.warheadImpactIntentId);
    runtimeStableHashMixString(hash, impact.trailerIntentId);
    runtimeStableHashMixString(
        hash,
        runtimeMultiplayerCanonicalIdentifier(impact.particleId)
    );
    runtimeStableHashMix(hash, impact.damage);
    runtimeMultiplayerAuthoritativeMixDouble(hash, impact.cellSpread);
}

std::string runtimeMultiplayerSnapshotImpactRecord(
    const RuntimeImpactEffect& impact) {
    std::ostringstream output;
    output << impact.projectileActorId << '|'
           << impact.attackerObjectId << '|'
           << impact.targetObjectId << '|'
           << runtimeBoolText(impact.targetIsStructure) << '|'
           << iniHexEncode(impact.owner) << '|'
           << impact.cell.x << '|' << impact.cell.y << '|'
           << impact.level << '|'
           << runtimeBoolText(impact.hasWorldPosition) << '|'
           << runtimeMultiplayerSnapshotCanonicalDoubleBits(impact.worldX)
           << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(impact.worldY)
           << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(impact.worldLevel)
           << '|' << impact.totalTicks << '|' << impact.remainingTicks
           << '|' << impact.animationFrameCount
           << '|' << impact.animationFrameDelayTicks
           << '|' << impact.animationStartFrame
           << '|' << impact.animationLoopStartFrame
           << '|' << impact.animationLoopEndFrame
           << '|' << impact.animationLoopCount
           << '|' << impact.animationEndFrame
           << '|' << impact.animationSequenceSteps
           << '|' << impact.animationMidpointWidth
           << '|' << impact.animationMidpointHeight
           << '|' << impact.lastSmudgeMidpointStep
           << '|' << runtimeBoolText(impact.death)
           << '|' << iniHexEncode(impact.lifecycleState)
           << '|' << iniHexEncode(impact.weaponId)
           << '|' << iniHexEncode(impact.weaponSlot)
           << '|' << iniHexEncode(impact.projectileId)
           << '|' << iniHexEncode(impact.warheadId)
           << '|' << iniHexEncode(impact.warheadImpactIntentId)
           << '|' << iniHexEncode(impact.trailerIntentId)
           << '|' << iniHexEncode(impact.particleId)
           << '|' << impact.damage
           << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(
                impact.cellSpread
              )
           << '|' << iniHexEncode(impact.deferredParticleAnimId)
           << '|' << iniHexEncode(impact.deferredParticleOwner)
           << '|' << impact.deferredParticleTicks
           << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(impact.muzzleAnchorX)
           << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(impact.muzzleAnchorY)
           << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(impact.muzzleAnchorLevel)
           << '|' << impact.muzzleFacing
           << '|' << impact.screenOffsetX << '|' << impact.screenOffsetY
           << '|' << impact.nativeFixedX << '|' << impact.nativeFixedY
           << '|' << runtimeBoolText(impact.bodyCenterAnchored)
           << '|' << iniHexEncode(runtimeImpactBodyCenterPresentationRecord(
                impact.bodyCenterPresentation, impact.bodyCenterSpriteOffsetY));
    return output.str();
}

bool parseRuntimeMultiplayerSnapshotImpactRecord(
    const std::string& value,
    RuntimeImpactEffect& impact) {
    const std::vector<std::string> fields = splitRuntimeSaveFields(value);
    if ((fields.size() != 36U && fields.size() != 39U && fields.size() != 43U && fields.size() != 45U && fields.size() != 47U && fields.size() != 48U && fields.size() != 49U)
        || !parseRuntimeInt(fields[0], impact.projectileActorId)
        || !parseRuntimeInt(fields[1], impact.attackerObjectId)
        || !parseRuntimeInt(fields[2], impact.targetObjectId)
        || !parseRuntimeStrictBool(fields[3], impact.targetIsStructure)
        || !iniHexDecode(fields[4], impact.owner, 256U)
        || !parseRuntimeInt(fields[5], impact.cell.x)
        || !parseRuntimeInt(fields[6], impact.cell.y)
        || !parseRuntimeInt(fields[7], impact.level)
        || !parseRuntimeStrictBool(fields[8], impact.hasWorldPosition)
        || !runtimeMultiplayerSnapshotDoubleFromBits(fields[9], impact.worldX)
        || !runtimeMultiplayerSnapshotDoubleFromBits(fields[10], impact.worldY)
        || !runtimeMultiplayerSnapshotDoubleFromBits(fields[11], impact.worldLevel)
        || !parseRuntimeInt(fields[12], impact.totalTicks)
        || !parseRuntimeInt(fields[13], impact.remainingTicks)
        || !parseRuntimeInt(fields[14], impact.animationFrameCount)
        || !parseRuntimeInt(fields[15], impact.animationFrameDelayTicks)
        || !parseRuntimeInt(fields[16], impact.animationStartFrame)
        || !parseRuntimeInt(fields[17], impact.animationLoopStartFrame)
        || !parseRuntimeInt(fields[18], impact.animationLoopEndFrame)
        || !parseRuntimeInt(fields[19], impact.animationLoopCount)
        || !parseRuntimeInt(fields[20], impact.animationEndFrame)
        || !parseRuntimeInt(fields[21], impact.animationSequenceSteps)
        || !parseRuntimeInt(fields[22], impact.animationMidpointWidth)
        || !parseRuntimeInt(fields[23], impact.animationMidpointHeight)
        || !parseRuntimeInt(fields[24], impact.lastSmudgeMidpointStep)
        || !parseRuntimeStrictBool(fields[25], impact.death)
        || !iniHexDecode(fields[26], impact.lifecycleState, 256U)
        || !iniHexDecode(fields[27], impact.weaponId, 256U)
        || !iniHexDecode(fields[28], impact.weaponSlot, 256U)
        || !iniHexDecode(fields[29], impact.projectileId, 256U)
        || !iniHexDecode(fields[30], impact.warheadId, 256U)
        || !iniHexDecode(fields[31], impact.warheadImpactIntentId, 256U)
        || !iniHexDecode(fields[32], impact.trailerIntentId, 256U)
        || !iniHexDecode(fields[33], impact.particleId, 256U)
        || !parseRuntimeInt(fields[34], impact.damage)
        || !runtimeMultiplayerSnapshotDoubleFromBits(
            fields[35],
            impact.cellSpread
        )
        || impact.totalTicks <= 0
        || impact.remainingTicks < 0
        || impact.remainingTicks > impact.totalTicks
        || impact.animationFrameCount < 0
        || impact.animationFrameDelayTicks <= 0
        || impact.animationSequenceSteps < 0
        || impact.animationMidpointWidth < 0
        || impact.animationMidpointHeight < 0
        || impact.cellSpread < 0.0) {
        return false;
    }
    impact.deferredParticleAnimId.clear();
    impact.deferredParticleOwner.clear();
    impact.deferredParticleTicks = -1;
    if (fields.size() >= 39U
        && (!iniHexDecode(fields[36], impact.deferredParticleAnimId, 256U)
            || !iniHexDecode(fields[37], impact.deferredParticleOwner, 256U)
            || !parseRuntimeInt(fields[38], impact.deferredParticleTicks)
            || impact.deferredParticleTicks < -1 || impact.deferredParticleTicks == 0
            || impact.deferredParticleTicks > impact.remainingTicks
            || (impact.deferredParticleTicks > 0 && (impact.deferredParticleAnimId.empty()
                || impact.lifecycleState != "VirusDeath" || !impact.death)))) return false;
    impact.muzzleAnchorX = impact.muzzleAnchorY = impact.muzzleAnchorLevel = 0.0;
    impact.muzzleFacing = -1;
    if (fields.size() >= 43U
        && (!runtimeMultiplayerSnapshotDoubleFromBits(fields[39], impact.muzzleAnchorX)
            || !runtimeMultiplayerSnapshotDoubleFromBits(fields[40], impact.muzzleAnchorY)
            || !runtimeMultiplayerSnapshotDoubleFromBits(fields[41], impact.muzzleAnchorLevel)
            || !parseRuntimeInt(fields[42], impact.muzzleFacing)
            || !std::isfinite(impact.muzzleAnchorX) || !std::isfinite(impact.muzzleAnchorY)
            || !std::isfinite(impact.muzzleAnchorLevel)
            || impact.muzzleFacing < -1 || impact.muzzleFacing > 255)) return false;
    if (impact.lifecycleState == "TransportMuzzle" && impact.muzzleFacing < 0) return false;
    impact.renderIntent = "impact";
    impact.screenOffsetX = impact.screenOffsetY = 0;
    impact.nativeFixedX = impact.nativeFixedY = 0;
    if (fields.size() >= 45U && (!parseRuntimeInt(fields[43], impact.screenOffsetX)
        || !parseRuntimeInt(fields[44], impact.screenOffsetY)
        || std::abs(impact.screenOffsetX) > 4096 || std::abs(impact.screenOffsetY) > 4096)) return false;
    if (fields.size() >= 47U && (!parseRuntimeInt(fields[45], impact.nativeFixedX)
        || !parseRuntimeInt(fields[46], impact.nativeFixedY)
        || std::abs(static_cast<long long>(impact.nativeFixedX)) > 65536
        || std::abs(static_cast<long long>(impact.nativeFixedY)) > 65536)) return false;
    impact.bodyCenterAnchored = false;
    if (fields.size() >= 48U && !parseRuntimeStrictBool(fields[47], impact.bodyCenterAnchored)) return false;
    impact.bodyCenterPresentation = {};
    impact.bodyCenterSpriteOffsetY = 0;
    if (fields.size() == 49U) {
        std::string presentation;
        if (!iniHexDecode(fields[48], presentation, 8192U)
            || !parseRuntimeImpactBodyCenterPresentationRecord(presentation,
                impact.bodyCenterPresentation, impact.bodyCenterSpriteOffsetY)
            || (!impact.bodyCenterAnchored && (impact.bodyCenterPresentation.enabled
                || impact.bodyCenterSpriteOffsetY != 0))) return false;
    }
    impact.radius = 10;
    impact.frameIndex = -1;
    impact.facing = -1;
    impact.projectileImageId.clear();
    impact.animId.clear();
    impact.bright = false;
    return true;
}

int runtimeVeterancyPromotionThreshold(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const double ratio = std::max(
        0.0,
        state.rules.getVeterancyRules().veteranRatio
    );
    const double threshold = std::ceil(
        static_cast<double>(runtimeVeterancyObjectCost(state, object) * ratio)
            + 1.0
    );
    return static_cast<int>(std::clamp(
        threshold,
        1.0,
        static_cast<double>(std::numeric_limits<int>::max())
    ));
}

bool awardRuntimeVeterancyExperience(
    RuntimeProductionHostState& state,
    int attackerObjectId,
    const std::string& attackerOwner,
    const RuntimeVeterancyKillCredit& credit) {
    if (!credit.awardsExperience
        || credit.victimCost <= 0
        || trimAsciiLocal(attackerOwner).empty()
        || runtimeOwnersAreAllied(
            state,
            attackerOwner,
            credit.victimOwner
        )) {
        return false;
    }

    WorldObjectInstance* attacker =
        findMutableRuntimeObjectByIdIncludingInactive(state, attackerObjectId);
    if (attacker == nullptr
        || !runtimeCombatSourceAvailable(state.rules, state.world.getObjects(), *attacker)
        || attacker->owner != attackerOwner
        || !runtimeVeterancyObjectIsTrainable(state, *attacker)) {
        return false;
    }

    const int cap = std::clamp(
        state.rules.getVeterancyRules().veteranCap,
        0,
        2
    );
    attacker->veterancy = std::clamp(attacker->veterancy, 0, 2);
    if (attacker->veterancy >= cap) {
        attacker->veterancyExperience = 0;
        return false;
    }

    const int threshold = runtimeVeterancyPromotionThreshold(state, *attacker);
    const bool hadCloakAbility = runtimeVeterancyHasAbility(
        state,
        *attacker,
        VeteranAbility::Cloak
    );
    const long long xpGain =
        static_cast<long long>(credit.victimCost)
        * static_cast<long long>(std::clamp(credit.victimVeterancy, 0, 2) + 1);
    const long long accumulated = std::min<long long>(
        std::numeric_limits<int>::max(),
        static_cast<long long>(std::max(0, attacker->veterancyExperience))
            + std::max<long long>(0, xpGain)
    );
    attacker->veterancyExperience = static_cast<int>(accumulated);

    const int availableLevels = cap - attacker->veterancy;
    const int levelIncrease = std::min(
        availableLevels,
        attacker->veterancyExperience / threshold
    );
    if (levelIncrease <= 0) {
        return true;
    }

    attacker->veterancy += levelIncrease;
    attacker->veterancyExperience -= levelIncrease * threshold;
    if (attacker->veterancy >= cap) {
        attacker->veterancyExperience = 0;
    }
    if (!hadCloakAbility
        && runtimeVeterancyHasAbility(
            state,
            *attacker,
            VeteranAbility::Cloak
        )) {
        runtimeDecloakObject(state, *attacker, "veterancyCloakGranted");
    }
    const bool elite = attacker->veterancy >= 2;
    const std::string soundKey =
        elite ? "UpgradeEliteSound" : "UpgradeVeteranSound";
    const std::string soundId =
        trimAsciiLocal(state.rules.getAudioVisualValue(soundKey));
    emitRuntimeSound(
        state,
        soundId.empty()
            ? (elite ? "sound:UpgradeElite" : "sound:UpgradeVeteran")
            : "sound:" + soundId
    );

    RuntimeReplayEventSnapshot promotionEvent;
    promotionEvent.kind = "UnitPromoted";
    promotionEvent.objectId = attacker->instanceId;
    promotionEvent.targetObjectId = credit.victimObjectId;
    promotionEvent.reason = elite ? "Elite" : "Veteran";
    promotionEvent.renderIntentId = elite
        ? "veterancy:elite"
        : "veterancy:veteran";
    promotionEvent.renderIntentHash =
        runtimeStableHashString(promotionEvent.renderIntentId);
    recordRuntimeReplayEvent(state, std::move(promotionEvent));

    state.simulationStatus =
        attacker->type
        + " #" + std::to_string(attacker->instanceId)
        + (elite ? " promoted to Elite" : " promoted to Veteran");
    if (attacker->owner == state.economy.owner) {
        state.economy.status = state.simulationStatus;
        state.interactionStatus = state.simulationStatus;
    }
    return true;
}

bool updateRuntimeWeaponFacingToward(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const WorldObjectInstance& target,
    bool forceFire) {
    if (runtimeObjectIsTurningInTankBunker(state, object)
        || (object.kind == WorldObjectKind::Structure && object.buildAnimTicksRemaining > 0)) return false;
    const RuntimeWeaponProfile aimingProfile = runtimeWeaponProfileForObject(state, object, &target, forceFire);
    const auto* lifecycle = aimingProfile.weapon == nullptr ? nullptr
        : state.rules.findWeaponLifecycleProfile(aimingProfile.weapon->id);
    if (lifecycle != nullptr && lifecycle->omniFire) return true;
    const UnitType* spinningUnit = findRuntimeUnitType(state.rules, object);
    if (spinningUnit != nullptr && spinningUnit->turretSpins) return true;
    const RuntimeCellPoint from = runtimeCellPointForObject(object);
    const RuntimeCellPoint to = runtimeCellPointForObject(target);
    const int desiredFacing =
        runtimeFacingFromDelta(to.x - from.x, to.y - from.y);
    if (desiredFacing < 0) {
        return true;
    }

    if (!runtimeObjectHasTurret(state, object)) {
        if (object.kind == WorldObjectKind::Structure) {
            object.turretFacing = -1;
            return true;
        }
        const int currentFacing =
            object.facing >= 0 ? object.facing : desiredFacing;
        const int nextFacing = runtimeAdvanceBodyFacing(state, object, currentFacing, desiredFacing);
        object.facing = nextFacing;
        object.turretFacing = -1;
        return runtimeFacingAbsDifference(nextFacing, desiredFacing) <= 8;
    }

    const int currentFacing =
        object.turretFacing >= 0
            ? object.turretFacing
            : (object.facing >= 0 ? object.facing : desiredFacing);
    const int nextFacing = runtimeAdvanceFacingClock(object.turretFacingClock,
        currentFacing, desiredFacing, runtimeTurretTurnStep(state, object), state.tick, state.config.fixedTickHz);
    object.turretFacing = nextFacing;
    if (nextFacing != currentFacing) ++state.actionSummary.turretTurnsThisTick;
    if (object.turretFacingClock.active())
        markRuntimeTurretSoundRotation(state, object);

    return nextFacing == desiredFacing;
}

bool updateRuntimeWeaponFacingTowardBody(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object) {
    if (runtimeObjectIsTurningInTankBunker(state, object)) return false;
    if (object.kind == WorldObjectKind::Structure) {
        // A fixed defense keeps its last aim. Its placement facing is not a
        // chassis heading to return to after losing or finishing a target.
        object.turretFacingClock = {};
        return true;
    }
    const UnitType* spinningUnit = findRuntimeUnitType(state.rules, object);
    if (spinningUnit != nullptr && spinningUnit->turretSpins) return true;
    if (!runtimeObjectHasTurret(state, object) || object.facing < 0) {
        return true;
    }

    const int desiredFacing = normalizeRuntimeFacing(object.facing);
    if (object.turretFacing < 0) {
        return true;
    }

    const int currentFacing = normalizeRuntimeFacing(object.turretFacing);
    const int nextFacing = runtimeAdvanceFacingClock(object.turretFacingClock,
        currentFacing, desiredFacing, runtimeTurretTurnStep(state, object), state.tick, state.config.fixedTickHz);
    object.turretFacing =
        nextFacing == desiredFacing ? -1 : nextFacing;
    if (nextFacing != currentFacing) ++state.actionSummary.turretTurnsThisTick;
    if (object.turretFacingClock.active())
        markRuntimeTurretSoundRotation(state, object);

    return nextFacing == desiredFacing;
}

bool advanceRuntimeIdleWeaponFacings(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeIdleWeaponFacings", state.tick);
    bool changed = false;
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive
            || runtimeObjectIsTurningInTankBunker(state, object)
            || !runtimeObjectHasTurret(state, object)
            || !runtimeObjectHasOperationalPower(state, object)) {
            continue;
        }

        const UnitType* unit = findRuntimeUnitType(state.rules, object);
        if (unit != nullptr && unit->turretSpins) {
            // Stock UnitClass::UpdateRotation (0x736A95): advance by 8/256
            // of a turn per authored frame, including while hovering/firing.
            if (state.specialMechanismsInitialized
                && state.specialMechanisms.isTemporalFrozen(object.instanceId)) continue;
            const int frames = runtimeDiskLaserAuthoredFrame(state.tick, state.config.fixedTickHz)
                - runtimeDiskLaserAuthoredFrame(state.tick - 1, state.config.fixedTickHz);
            if (frames > 0) {
                object.turretFacing = normalizeRuntimeFacing(
                    (object.turretFacing >= 0 ? object.turretFacing : object.facing) + 8 * frames);
                changed = true;
            }
            continue;
        }
        const auto behaviorIt = state.behaviors.find(object.instanceId);
        if (behaviorIt != state.behaviors.end()
            && behaviorIt->second.kind == RuntimeBehaviorKind::Attack) {
            continue;
        }

        const int previousFacing = object.turretFacing;
        const bool previousClockActive = object.turretFacingClock.active();
        (void)updateRuntimeWeaponFacingTowardBody(state, object);
        changed = changed || object.turretFacing != previousFacing
            || (previousClockActive && !object.turretFacingClock.active());
    }
    return changed;
}

bool runtimeWarheadCanDamageWallTarget(
    const RuntimeGameSessionState& state,
    const WarheadType* warhead,
    const WorldObjectInstance& target) {
    if (target.kind != WorldObjectKind::Structure) {
        return true;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, target.type);
    if (building == nullptr || !building->wall) {
        return true;
    }
    const bool woodWall =
        toLowerAsciiLocal(trimAsciiLocal(building->armor)) == "wood";
    return warhead != nullptr
        && (warhead->wall || (warhead->wood && woodWall));
}

int runtimeAuthoredDamageAgainst(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target,
    long long signedBaseDamage,
    const std::string& warheadId,
    double distanceFromImpact,
    double outgoingDamageMultiplier,
    bool attachedIvanBuildingBomb) {
    if (signedBaseDamage == 0
        || (signedBaseDamage > 0 && runtimeCarrierAircraftHasDeckProtection(state, target))) {
        return 0;
    }
    const WarheadType* warhead =
        findRuntimeWarheadType(state.rules, warheadId);
    if (signedBaseDamage > 0 && warhead != nullptr && warhead->poison
        && runtimeObjectIsPoisonImmune(state, target)) {
        return 0;
    }
    if (signedBaseDamage > 0
        && !attachedIvanBuildingBomb
        && !runtimeWarheadCanDamageWallTarget(state, warhead, target)) {
        return 0;
    }
    double rangeMultiplier = 1.0;
    if (warhead != nullptr && warhead->cellSpread > 0.0) {
        const double maxDistance = std::max(0.1, warhead->cellSpread);
        const double clampedDistance =
            std::clamp(distanceFromImpact, 0.0, maxDistance);
        const double percentAtMax =
            runtimeWarheadPercentAtMaxFraction(*warhead);
        const double t = clampedDistance / maxDistance;
        rangeMultiplier = 1.0 - ((1.0 - percentAtMax) * t);
    }
    const double armorMultiplier =
        runtimeWarheadArmorMultiplier(warhead, runtimeTargetEffectiveArmor(state, target, warhead));
    const bool targetIsProneInfantry =
        target.kind == WorldObjectKind::Infantry
        && target.prone && !runtimeActiveShield(target, state.rules, warhead);
    const double proneMultiplier =
        signedBaseDamage > 0
            && warhead != nullptr
            && !warhead->radiation
            && !warhead->temporal
            && targetIsProneInfantry
        ? std::max(0.0, warhead->proneDamageMultiplier)
        : 1.0;
    double targetVeteranArmor = 1.0;
    const auto* shieldArmor = runtimeActiveShield(target, state.rules, warhead);
    if (signedBaseDamage > 0 && (!shieldArmor || shieldArmor->applyArmorMult)) {
        targetVeteranArmor = std::max(
            0.01,
            runtimeVeterancyAbilityMultiplier(
                state,
                target,
                VeteranAbility::Stronger
            )
        );
    }
    const double effectiveOutgoingMultiplier =
        std::max(0.0, outgoingDamageMultiplier);
    if (armorMultiplier <= 0.0
        || rangeMultiplier <= 0.0
        || proneMultiplier <= 0.0
        || effectiveOutgoingMultiplier <= 0.0) {
        return 0;
    }
    const double scaledMagnitude =
        std::abs(static_cast<double>(signedBaseDamage))
        * armorMultiplier
        * rangeMultiplier
        * proneMultiplier
        * effectiveOutgoingMultiplier
        / targetVeteranArmor;
    const double maximumMagnitude = static_cast<double>(
        std::numeric_limits<int>::max()
    );
    const int magnitude = !std::isfinite(scaledMagnitude)
            || scaledMagnitude >= maximumMagnitude
        ? std::numeric_limits<int>::max()
        : std::max(0, static_cast<int>(std::round(scaledMagnitude)));
    return signedBaseDamage < 0 ? -magnitude : magnitude;
}

int runtimeWeaponDamageAgainst(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target,
    const WeaponType& weapon,
    double distanceFromImpact,
    double outgoingDamageMultiplier) {
    return runtimeAuthoredDamageAgainst(
        state,
        target,
        static_cast<long long>(weapon.damage),
        weapon.warhead,
        distanceFromImpact,
        outgoingDamageMultiplier
    );
}

bool runtimeProjectileCanHitTarget(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& target) {
    if (runtimeCarrierAircraftHasDeckProtection(state, target)) return false;
    const bool airTarget = runtimeObjectIsAirTarget(state, target);
    if (airTarget && !projectile.aa) {
        return false;
    }
    if (!airTarget && !projectile.ag) {
        return false;
    }
    return true;
}

bool runtimeWeaponCanTargetObject(
    const RuntimeGameSessionState& state,
    const WeaponType& weapon,
    const WorldObjectInstance& target) {
    if (runtimeCarrierAircraftHasDeckProtection(state, target)) return false;
    if (weapon.drainWeapon) {
        const auto* building = target.kind == WorldObjectKind::Structure
            ? state.rules.findBuildingType(target.type) : nullptr;
        if (!building || !building->drainable) return false;
    }
    const ProjectileType* projectile =
        findRuntimeProjectileType(state.rules, weapon.projectile);
    const bool airTarget = runtimeObjectIsAirTarget(state, target);
    if (projectile == nullptr) {
        return false;
    }
    const bool aa = projectile->aa;
    const bool ag = projectile->ag;
    if (airTarget && !aa) {
        return false;
    }
    // A spawner projectile is a targeting marker (ASWVirt has AG=no).
    // NavalTargeting/LandTargeting still choose the appropriate weapon slot.
    if (!airTarget && !ag && !weapon.spawner) {
        return false;
    }
    const WarheadType* warhead =
        findRuntimeWarheadType(state.rules, weapon.warhead);
    if (warhead != nullptr && warhead->bombDisarm) {
        // DefuseKit has Damage=1, but is a target-specific ability. Without
        // a bomb it must not become an ordinary attack after a rejected
        // engineer repair/capture, in either the cursor or command path.
        return state.specialMechanismsInitialized
            && state.specialMechanisms.timedBombs().contains(target.instanceId);
    }
    if (warhead != nullptr && warhead->ivanBomb
        && target.kind == WorldObjectKind::Structure
        && findRuntimeBuildingTypeById(state.rules, target.type) != nullptr) {
        // Planting is a relationship, independent of ordinary wall/armor damage.
        return true;
    }
    if (runtimeWarheadArmorMultiplier(
            warhead,
            runtimeTargetEffectiveArmor(state, target, warhead)
        ) <= 0.0) {
        return false;
    }
    if (!runtimeWarheadCanDamageWallTarget(state, warhead, target)) {
        return false;
    }
    return true;
}

bool runtimeWeaponCanTargetObjectFromSource(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const WeaponType& weapon,
    const WorldObjectInstance& target) {
    const BuildingType* wall = target.kind == WorldObjectKind::Structure
        ? findRuntimeBuildingTypeById(state.rules, target.type) : nullptr;
    const SpawnedMissileProfile* missile = wall != nullptr && wall->wall
        ? runtimeSpawnedMissileProfileForWeapon(state, source, weapon) : nullptr;
    if (missile == nullptr) {
        return runtimeWeaponCanTargetObject(state, weapon, target);
    }
    const bool elite = source.veterancy >= 2;
    const std::string& warheadId = elite ? missile->eliteWarhead : missile->warhead;
    const WarheadType* warhead = findRuntimeWarheadType(state.rules, warheadId);
    if (warhead == nullptr || (elite ? missile->eliteDamage : missile->damage) <= 0) {
        return false;
    }
    // The spawner weapon is a target marker. Only wall-target damage
    // qualification uses the actual missile payload; projectile/domain,
    // range, minimum range, launch cadence and firing authority stay authored.
    WeaponType targetingWeapon = weapon;
    targetingWeapon.warhead = warheadId;
    return runtimeWeaponCanTargetObject(state, targetingWeapon, target);
}

bool runtimeWarheadCanAffectTarget(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target) {
    const bool allied =
        runtimeOwnersAreAllied(state, attacker.owner, target.owner);
    if (allied && !projectile.affectsAllies) {
        return target.instanceId == projectile.targetObjectId;
    }
    if (!allied && !projectile.affectsEnemies) {
        return target.instanceId == projectile.targetObjectId;
    }
    return true;
}

int runtimeWeaponCooldownTicks(const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker, const WeaponType& weapon) {
    // Retail GetROF (0x6FCFA0): these continuous emitters use the raw delay.
    // Ordinary completed volleys add 0..2 rule frames before ROF bonuses.
    const bool continuous = weapon.sonic || weapon.isRailgun || weapon.useFireParticles || weapon.useSparkParticles;
    const auto seed = runtimeCombatRandomMix(static_cast<std::uint32_t>(state.nextProjectileActorId)
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(attacker.instanceId)) << 32U)
        ^ runtimeCombatRandomMix(static_cast<std::uint64_t>(state.config.simulationRandomSeed)) ^ 0x524f46ULL);
    const int ordinaryFrames = static_cast<int>(std::min<std::int64_t>(std::numeric_limits<int>::max(),
        static_cast<std::int64_t>(std::max(0, weapon.rof)) + seed % 3));
    const int frames = continuous ? std::max(0, weapon.rof)
        : runtimeRofScaledFrames(ordinaryFrames,
            runtimeVeterancyAbilityMultiplier(state, attacker, VeteranAbility::Rof));
    return runtimeRuleFramesToTicks(std::max(1, frames), state.config.fixedTickHz);
}

int runtimeWeaponCooldownTicks(const WeaponType& weapon, int fixedTickHz) {
    return runtimeRuleFramesToTicks(std::max(1, weapon.rof), fixedTickHz);
}

int nextRuntimeProjectileActorId(RuntimeGameSessionState& state) {
    int candidate = state.nextProjectileActorId;
    if (candidate <= 0 || candidate >= std::numeric_limits<int>::max()) {
        candidate = 1;
    }
    const auto actorIdInUse = [&state](int actorId) {
        for (const RuntimeProjectileEffect& projectile : state.projectiles) {
            if (projectile.actorId == actorId) {
                return true;
            }
        }
        for (const RuntimeProjectileEffect& projectile :
                state.recentExpiredProjectiles) {
            if (projectile.actorId == actorId) {
                return true;
            }
        }
        for (const RuntimeImpactEffect& impact : state.impactEffects) {
            if (impact.projectileActorId == actorId) {
                return true;
            }
        }
        return false;
    };
    const std::size_t maximumAttempts = state.projectiles.size()
        + state.recentExpiredProjectiles.size()
        + state.impactEffects.size()
        + 1U;
    for (std::size_t attempt = 0; attempt < maximumAttempts; ++attempt) {
        if (!actorIdInUse(candidate)) {
            state.nextProjectileActorId =
                candidate >= std::numeric_limits<int>::max() - 1
                    ? 1
                    : candidate + 1;
            return candidate;
        }
        candidate = candidate >= std::numeric_limits<int>::max() - 1
            ? 1
            : candidate + 1;
    }
    // The number of retained actors is tiny relative to the positive int
    // domain, so reaching this path indicates corrupted bookkeeping.  Keep
    // the allocator positive and deterministic instead of overflowing.
    state.nextProjectileActorId = 2;
    return 1;
}

double runtimeProjectileProgress(const RuntimeProjectileEffect& projectile) {
    const int totalTicks = std::max(1, projectile.totalTicks);
    return std::clamp(
        static_cast<double>(projectile.elapsedTicks)
            / static_cast<double>(totalTicks),
        0.0,
        1.0
    );
}

void rememberRuntimeExpiredProjectile(
    RuntimeGameSessionState& state,
    RuntimeProjectileEffect projectile) {
    projectile.lifecycle = RuntimeProjectileLifecycleState::Expired;
    projectile.lifecycleState =
        runtimeProjectileLifecycleName(projectile.lifecycle);
    projectile.elapsedTicks = std::max(1, projectile.totalTicks);
    if (!projectile.voxelDebris.enabled
        && !runtimeProjectileIsParasiteLeap(state, projectile)) {
        updateRuntimeProjectileTravelPosition(projectile);
    }
    state.recentExpiredProjectiles.push_back(std::move(projectile));
    constexpr std::size_t maxRecentExpiredProjectiles = 32;
    while (state.recentExpiredProjectiles.size()
        > maxRecentExpiredProjectiles) {
        state.recentExpiredProjectiles.pop_front();
    }
}

std::string runtimeProjectileReplayRenderIntentId(
    const RuntimeProjectileEffect& projectile,
    const std::string& kind) {
    if (kind == "ProjectileImpact") {
        return "impact:"
            + projectile.impactAnimId
            + ":"
            + projectile.warheadImpactIntentId;
    }
    if (kind == "ProjectileExpired") {
        return "expired:"
            + projectile.impactAnimId
            + ":"
            + projectile.warheadImpactIntentId;
    }
    return projectile.renderIntent
        + ":"
        + projectile.projectileImageId
        + ":"
        + projectile.trailerIntentId;
}

void recordRuntimeProjectileReplayEvent(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const std::string& kind) {
    RuntimeReplayEventSnapshot event;
    event.kind = kind;
    event.objectId = projectile.attackerObjectId;
    event.targetObjectId = projectile.targetObjectId;
    event.projectileActorId = projectile.actorId;
    event.weaponId = projectile.weaponId;
    event.weaponSlot = projectile.weaponSlot;
    event.projectileId = projectile.projectileId;
    event.projectileImageId = projectile.projectileImageId;
    event.warheadId = projectile.warheadId;
    event.lifecycleState = projectile.lifecycleState;
    event.spawnTick = projectile.spawnTick;
    event.impactTick = projectile.impactTick;
    if (kind == "ProjectileExpired") {
        event.expiredTick = state.tick;
    }
    event.renderIntentId =
        runtimeProjectileReplayRenderIntentId(projectile, kind);
    event.renderIntentHash = runtimeStableHashString(event.renderIntentId);
    event.trailerIntentId = projectile.trailerIntentId;
    event.impactIntentId = projectile.impactAnimId;
    event.warheadImpactIntentId = projectile.warheadImpactIntentId;
    event.sourceCell = toRuntimeSnapshotCell(projectile.fromCell);
    event.targetCell = toRuntimeSnapshotCell(projectile.toCell);
    event.currentCell = toRuntimeSnapshotCell(projectile.currentCell);
    recordRuntimeReplayEvent(state, std::move(event));
}

bool runtimeProjectileIsNuclearDeathDetonation(const RuntimeProjectileEffect& projectile) {
    // The reactor borrows the nuclear payload, whose projectile declaration
    // also names the strategic missile sprite. A death explosion has no missile.
    return projectile.deathWeaponRequestId != 0 && projectile.deathWeaponCascadeId != 0
        && !projectile.voxelDebris.enabled
        && toLowerAsciiLocal(projectile.projectileImageId) == "nkmsldn";
}

bool runtimeProjectileIsCrashDeathDetonation(const RuntimeProjectileEffect& projectile) {
    // BlimpBombEffect supplies a crash's payload, not an extra dropped bomb.
    // Explicit bomb attacks and voxel wreck/debris presentation remain visible.
    return projectile.deathWeaponRequestId != 0 && projectile.deathWeaponCascadeId != 0
        && !projectile.voxelDebris.enabled && projectile.vertical
        && toLowerAsciiLocal(projectile.weaponId) == "blimpbombeffect";
}

std::optional<std::array<double, 3>> runtimeTransportMuzzleAnchor(
    const RuntimeGameSessionState& state, int shooterId) {
    const auto* source = findRuntimeObjectByIdIncludingInactive(state, shooterId);
    if (!source || source->health <= 0) return std::nullopt;
    const WorldObjectInstance* anchor = source;
    if (!source->alive) {
        if (source->mission != "InsideTransport") return std::nullopt;
        anchor = nullptr;
        for (const auto& host : state.world.getObjects()) {
            if (host.alive && host.health > 0 && std::find(host.transportPassengerIds.begin(),
                    host.transportPassengerIds.end(), shooterId) != host.transportPassengerIds.end()) {
                anchor = &host; break;
            }
        }
        if (!anchor) return std::nullopt;
    }
    const auto subCell = worldObjectSubCellOffset(*anchor);
    return std::array<double, 3>{runtimeObjectRenderX(*anchor) + subCell.x,
        runtimeObjectRenderY(*anchor) + subCell.y, runtimeDrawLevelForObject(*anchor)};
}

RuntimeImpactRenderActor makeRuntimeImpactRenderActor(
    const RuntimeImpactEffect& impact) {
    RuntimeImpactRenderActor actor;
    actor.actorId = impact.projectileActorId;
    actor.projectileActorId = impact.projectileActorId;
    actor.owner = impact.owner;
    actor.cellX = impact.cell.x;
    actor.cellY = impact.cell.y;
    actor.attackerObjectId = impact.attackerObjectId;
    actor.targetObjectId = impact.targetObjectId;
    actor.animId = impact.animId;
    actor.projectileImageId = impact.projectileImageId.empty() && impact.renderIntent == "missileExhaust"
        ? impact.projectileId : impact.projectileImageId;
    actor.particleId = impact.particleId;
    actor.weaponId = impact.weaponId;
    actor.warheadId = impact.warheadId;
    actor.lifecycleState = impact.lifecycleState;
    actor.renderIntent = impact.renderIntent;
    // Impact frames share the projectile endpoint as their global canvas
    // centre. A foot anchor shifts ground flashes/flames by their canvas
    // height; body-targeted impacts refine this same anchor in the renderer.
    actor.centerOnAnchor = impact.targetIsStructure
        || impact.renderIntent == "warheadImpact"
        || impact.renderIntent == "bombImpact"
        || impact.renderIntent == "deathImpact"
        || impact.lifecycleState == "MindControlImpact"
        || impact.lifecycleState == "ChronoWarpOut"
        || impact.lifecycleState == "ChronoWarpIn" || impact.lifecycleState == "ChronoReinforcementIn"
        || impact.lifecycleState == "TemporalErase"
        || impact.renderIntent == "buildingDestruction"
        || impact.renderIntent == "unitDestruction"
        || impact.renderIntent == "oreGather"
        || impact.renderIntent == "refinerySmoke"
        || impact.renderIntent == "weaponAnim"
        || impact.renderIntent == "missileExhaust"
        || impact.renderIntent == "superWeaponEffect";
    actor.x = impact.hasWorldPosition
        ? impact.worldX
        : static_cast<double>(impact.cell.x);
    actor.y = impact.hasWorldPosition
        ? impact.worldY
        : static_cast<double>(impact.cell.y);
    actor.level = impact.hasWorldPosition ? impact.worldLevel : impact.level;
    actor.screenOffsetX = impact.screenOffsetX;
    actor.nativeFixedX = impact.nativeFixedX;
    actor.screenOffsetY = impact.screenOffsetY;
    actor.nativeFixedY = impact.nativeFixedY;
    actor.bodyCenterAnchored = impact.bodyCenterAnchored;
    actor.bodyCenterPresentation = impact.bodyCenterPresentation;
    actor.bodyCenterSpriteOffsetY = impact.bodyCenterSpriteOffsetY;
    actor.totalTicks = impact.totalTicks;
    actor.remainingTicks = impact.remainingTicks;
    actor.fadeTicks = impact.lifecycleState == "InfantryDeath"
        ? std::max(1, (impact.totalTicks - impact.animationSequenceSteps
            * impact.animationFrameDelayTicks) / 2) : 0;
    actor.frameIndex = impact.frameIndex;
    if (impact.lifecycleState == "InfantryDeath") {
        actor.renderIntent = "infantryDeath";
        actor.frameIndex = impact.animationStartFrame + std::clamp(
            (impact.totalTicks - impact.remainingTicks)
                / std::max(1, impact.animationFrameDelayTicks),
            0, std::max(0, impact.animationSequenceSteps - 1));
    }
    if (impact.lifecycleState == "VirusDeath") {
        actor.animId = impact.deferredParticleAnimId;
        actor.renderIntent = "unitDestruction";
        actor.centerOnAnchor = true;
        actor.frameIndex = runtimeImpactAnimationFrameForStep(impact,
            (impact.totalTicks - impact.remainingTicks) / std::max(1, impact.animationFrameDelayTicks));
    }
    if (impact.lifecycleState == "SquidSplash") {
        actor.animId = impact.warheadImpactIntentId;
        actor.renderIntent = "squidSplash";
        actor.centerOnAnchor = true;
        actor.frameIndex = runtimeImpactAnimationFrameForStep(impact,
            (impact.totalTicks - impact.remainingTicks) / std::max(1, impact.animationFrameDelayTicks));
    }
    if (impact.lifecycleState == "GrindingAnimation"
        || impact.lifecycleState == "GrindingAnimationQueued") {
        actor.renderIntent = impact.lifecycleState == "GrindingAnimation"
            ? "grinderCycle" : "grinderCycleQueued";
        actor.frameIndex = runtimeImpactAnimationFrameForStep(impact,
            (impact.totalTicks - impact.remainingTicks) / std::max(1, impact.animationFrameDelayTicks));
    }
    actor.facing = impact.facing;
    actor.damage = impact.damage;
    actor.bright = impact.bright;
    return actor;
}

const std::vector<RuntimeImpactRenderActor>* runtimeImpactRenderActorsForRender(
    const RuntimeProductionHostState& state) {

    RuntimeSnapshotCache& cache = state.snapshotCache;
    cache.impactRenderActors.clear();
    cache.impactRenderActors.reserve(state.impactEffects.size());
    for (const RuntimeImpactEffect& impact : state.impactEffects) {
        if (!runtimeChronosphereSelectionVisibleToLocalOwner(state, impact)) continue;
        if (impact.lifecycleState == "GrindingAnimationQueued"
            || (impact.lifecycleState == "GrindingAnimation" && impact.remainingTicks<=0)) continue;
        RuntimeImpactRenderActor actor = makeRuntimeImpactRenderActor(impact);
        // Snapshot transfer deliberately omits cached presentation fields.
        // Dead objects retain their type identity, so rebuild the corpse image
        // without changing authoritative state or restarting its timeline.
        if (impact.lifecycleState == "InfantryDeath" && actor.animId.empty()) {
            const WorldObjectInstance* object = findRuntimeObjectByIdIncludingInactive(
                state, impact.targetObjectId);
            const UnitType* unit = object == nullptr ? nullptr
                : state.rules.findUnitType("InfantryType", object->type);
            if (unit != nullptr) {
                const ArtType* art = runtimeArtTypeForObject(state, *object);
                if (art == nullptr) art = state.art.findArtType(unit->id);
                if (art != nullptr) actor.animId = art->id;
            }
        }
        if (impact.lifecycleState == "UnitDestruction" && actor.animId.empty()) {
            // Network snapshots omit cached images. Rebuild the special
            // infantry death from its lethal warhead and original unit type.
            const auto* object = findRuntimeObjectByIdIncludingInactive(state, impact.targetObjectId);
            const auto* unit = object && object->kind == WorldObjectKind::Infantry
                ? state.rules.findUnitType("InfantryType", object->type) : nullptr;
            const auto* warhead = state.rules.findWarheadType(impact.warheadId);
            if (unit && !unit->notHuman && warhead) {
                const auto& animation = state.rules.getInfantryDeathAnim(warhead->infDeath);
                if (!animation.empty() && state.art.findArtType(animation)) {
                    actor.animId = animation;
                    actor.renderIntent = "unitDestruction";
                    actor.centerOnAnchor = true;
                    actor.frameIndex = runtimeImpactAnimationFrameForStep(impact,
                        (impact.totalTicks - impact.remainingTicks) / std::max(1, impact.animationFrameDelayTicks));
                }
            }
        }
        if (impact.lifecycleState == "ChronoWarpOut" || impact.lifecycleState == "ChronoWarpIn" || impact.lifecycleState == "ChronoReinforcementIn") {
            const auto& rules = state.rules.getSpecialMechanismRules();
            actor.animId = impact.lifecycleState == "ChronoReinforcementIn" ? rules.warpInAnim : rules.warpOutAnim;
            actor.renderIntent = "chronoWarp";
            actor.centerOnAnchor = true;
        }
        if (impact.lifecycleState == "OccupantMuzzle" || impact.lifecycleState == "TransportMuzzle") {
            const WeaponType* weapon = findRuntimeWeaponType(state.rules, impact.weaponId);
            if (impact.lifecycleState == "TransportMuzzle") {
                const auto anchor = runtimeTransportMuzzleAnchor(state, impact.attackerObjectId);
                if (!anchor) continue;
                actor.x += (*anchor)[0] - impact.muzzleAnchorX;
                actor.y += (*anchor)[1] - impact.muzzleAnchorY;
                actor.level += (*anchor)[2] - impact.muzzleAnchorLevel;
                if (actor.animId.empty() && weapon) {
                    actor.animId = runtimeWeaponAnimForFacing(*weapon, impact.muzzleFacing);
                    if (actor.animId.empty()) actor.animId = trimAsciiLocal(weapon->openToppedAnim);
                }
            } else if (actor.animId.empty() && weapon) actor.animId = trimAsciiLocal(weapon->occupantAnim);
            actor.renderIntent = "weaponAnim";
            actor.centerOnAnchor = true;
            if (impact.animationSequenceSteps > 0) actor.frameIndex = runtimeImpactAnimationFrameForStep(impact,
                std::max(0, impact.totalTicks - impact.remainingTicks - 1)
                    / std::max(1, impact.animationFrameDelayTicks));
        }
        if (impact.lifecycleState == "GattlingMuzzle" && impact.animationSequenceSteps > 0) {
            actor.frameIndex = runtimeImpactAnimationFrameForStep(impact,
                std::max(0, impact.totalTicks - impact.remainingTicks - 1)
                    / std::max(1, impact.animationFrameDelayTicks));
        }
        cache.impactRenderActors.push_back(std::move(actor));
    }
#include "simulation/mod_extensions/RuntimeModExtensionRenderActors.inl"
#include "render/effects/RuntimeDiskDrainRenderActors.inl"
    if (const auto marker = runtimeLocalChronosphereRenderActor(state)) {
        cache.impactRenderActors.push_back(*marker);
    }
    ++cache.stats.impactRenderActorMaterializations;
    cache.stats.lastImpactRenderActors =
        static_cast<int>(cache.impactRenderActors.size());
    return &cache.impactRenderActors;
}

std::optional<RuntimeWeaponFireFlh> runtimeFallbackWeaponFireFlh(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const RuntimeWeaponProfile& profile) {
    if (profile.slot == RuntimeWeaponSlot::None
        || ((profile.slot == RuntimeWeaponSlot::Indexed
                || profile.slot == RuntimeWeaponSlot::EliteIndexed)
            && profile.indexedWeaponIndex < 0)) {
        // Indexed profiles without an authored index are synthetic impact
        // weapons (shrapnel, airburst, death weapons). Their origin is the
        // impact coordinate, not a unit muzzle.
        return std::nullopt;
    }

    const UnitType* unit = findRuntimeUnitType(state.rules, attacker);
    if (unit == nullptr) {
        return std::nullopt;
    }

    RuntimeWeaponFireFlh flh;
    if (profile.weapon != nullptr && profile.weapon->areaFire) {
        const auto* warhead = findRuntimeWarheadType(state.rules, profile.weapon->warhead);
        if (warhead != nullptr && warhead->psychedelic) {
            flh.heightLeptons = 48;
            return flh;
        }
    }
    if (attacker.kind == WorldObjectKind::Infantry) {
        // The stock GI/E1 coordinate is the neutral infantry rifle pose and
        // is a substantially safer fallback than the unit's feet/body pivot.
        flh.forwardLeptons = 80;
        flh.heightLeptons = 100;
        return flh;
    }

    if (unit->turret) {
        // Stock medium tanks cluster around 150,0,100. This places mod units
        // with omitted art metadata at the visible end of a conventional
        // turret barrel instead of the hull origin.
        flh.forwardLeptons = 150;
        flh.heightLeptons = 100;
        return flh;
    }

    if (attacker.authoredAircraft
        || unit->consideredAircraft
        || unit->jumpJet
        || unit->balloonHover) {
        flh.forwardLeptons = 96;
        flh.heightLeptons = 48;
        return flh;
    }

    flh.forwardLeptons = unit->naval || unit->waterBound ? 128 : 112;
    flh.heightLeptons = unit->naval || unit->waterBound ? 48 : 80;
    return flh;
}

std::optional<RuntimeWeaponFireFlh> runtimeWeaponFireFlh(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const RuntimeWeaponProfile& profile,
    const ArtType* art) {
    if (profile.slot == RuntimeWeaponSlot::None
        || ((profile.slot == RuntimeWeaponSlot::Indexed
                || profile.slot == RuntimeWeaponSlot::EliteIndexed)
            && profile.indexedWeaponIndex < 0)) {
        return std::nullopt;
    }

    std::vector<const std::string*> candidates;
    candidates.reserve(8);
    const auto addCandidate = [&candidates](const std::string& value) {
        if (trimAsciiLocal(value).empty()) {
            return;
        }
        const auto duplicate = std::find_if(
            candidates.begin(),
            candidates.end(),
            [&value](const std::string* candidate) {
                return candidate == &value || *candidate == value;
            }
        );
        if (duplicate == candidates.end()) {
            candidates.push_back(&value);
        }
    };
    const auto addStandardCandidates = [
        art,
        &addCandidate
    ](bool elite, bool secondary) {
        if (art == nullptr) {
            return;
        }
        if (secondary) {
            if (elite) {
                addCandidate(art->eliteSecondaryFireFLH);
            }
            addCandidate(art->secondaryFireFLH);
            if (elite) {
                addCandidate(art->elitePrimaryFireFLH);
            }
            addCandidate(art->primaryFireFLH);
        } else {
            if (elite) {
                addCandidate(art->elitePrimaryFireFLH);
            }
            addCandidate(art->primaryFireFLH);
            if (elite) {
                addCandidate(art->eliteSecondaryFireFLH);
            }
            addCandidate(art->secondaryFireFLH);
        }
    };

    if (art != nullptr
        && (profile.slot == RuntimeWeaponSlot::Indexed
            || profile.slot == RuntimeWeaponSlot::EliteIndexed)) {
        const std::size_t index = static_cast<std::size_t>(
            profile.indexedWeaponIndex
        );
        if (profile.slot == RuntimeWeaponSlot::EliteIndexed
            && index < art->eliteWeaponFireFLHs.size()) {
            addCandidate(art->eliteWeaponFireFLHs[index]);
        }
        if (index < art->weaponFireFLHs.size()) {
            addCandidate(art->weaponFireFLHs[index]);
        }
        addStandardCandidates(
            profile.slot == RuntimeWeaponSlot::EliteIndexed,
            index == 1U
        );
    } else {
        bool elite = profile.slot == RuntimeWeaponSlot::ElitePrimary
            || profile.slot == RuntimeWeaponSlot::EliteSecondary
            || profile.slot == RuntimeWeaponSlot::EliteDeploy
            || profile.slot == RuntimeWeaponSlot::EliteOccupy;
        bool secondary = profile.slot == RuntimeWeaponSlot::Secondary
            || profile.slot == RuntimeWeaponSlot::EliteSecondary;

        const UnitType* unit = findRuntimeUnitType(state.rules, attacker);
        const BuildingType* building =
            findRuntimeBuildingType(state.rules, attacker);
        if (profile.slot == RuntimeWeaponSlot::Deploy
            || profile.slot == RuntimeWeaponSlot::EliteDeploy) {
            const std::string source = unit != nullptr
                ? (elite
                    ? unit->eliteDeployWeaponSource
                    : unit->deployWeaponSource)
                : (building != nullptr
                    ? (elite
                        ? building->eliteDeployWeaponSource
                        : building->deployWeaponSource)
                    : std::string{});
            const std::string normalizedSource =
                toLowerAsciiLocal(trimAsciiLocal(source));
            secondary = normalizedSource.find("secondary")
                != std::string::npos;
        } else if (profile.slot == RuntimeWeaponSlot::OpenTransport
            && unit != nullptr) {
            secondary = unit->openTransportWeapon == 1
                || unit->openTransportWeapon == 3;
            elite = unit->openTransportWeapon == 2
                || unit->openTransportWeapon == 3;
        }
        addStandardCandidates(elite, secondary);
    }

    for (const std::string* candidate : candidates) {
        const std::optional<RuntimeWeaponFireFlh> parsed =
            parseRuntimeWeaponFireFlh(*candidate);
        if (parsed.has_value()) {
            return parsed;
        }
    }
    return runtimeFallbackWeaponFireFlh(state, attacker, profile);
}

int runtimeWeaponFiringFacing(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target) {
    if (runtimeObjectHasTurret(state, attacker)
        && attacker.turretFacing >= 0) {
        return normalizeRuntimeFacing(attacker.turretFacing);
    }
    if (attacker.kind == WorldObjectKind::Structure) {
        const RuntimeCellPoint from = runtimeCellPointForObject(attacker);
        const RuntimeCellPoint to = runtimeCellPointForObject(target);
        const int desiredFacing =
            runtimeFacingFromDelta(to.x - from.x, to.y - from.y);
        if (desiredFacing >= 0) {
            return desiredFacing;
        }
    }
    return normalizeRuntimeFacing(attacker.facing >= 0 ? attacker.facing : 0);
}

double runtimeWeaponHeightInLevels(int heightLeptons) {
    // Authored FLH uses vertical leptons, not terrain elevation steps. At
    // 60x30, native Z projects by 60*sin(60deg)/hypot(256,256) pixels per
    // lepton; one renderer level is 15 pixels. Their ratio is sqrt(6)/256.
    constexpr double levelsPerHeightLepton = 2.449489742783178 / 256.0;
    return static_cast<double>(heightLeptons) * levelsPerHeightLepton;
}

RuntimeProjectileSourcePosition runtimeProjectileSourcePosition(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker,
    const RuntimeWeaponProfile& profile,
    int firingFacing) {
    RuntimeProjectileSourcePosition source;
    source.x = runtimeObjectRenderX(attacker);
    source.y = runtimeObjectRenderY(attacker);
    source.z = runtimeDrawLevelForObject(attacker);
    // Aircraft bodies have a shared presentation lift beyond their world
    // altitude. Muzzles must use the same origin, including released bombs.
    source.screenOffsetY = -runtimeAircraftSpriteLiftPixels(attacker, attacker.airborne);
    source.nativeFixedY = source.screenOffsetY;
    const WorldObjectCellOffset subCellOffset =
        worldObjectSubCellOffset(attacker);
    source.x += subCellOffset.x;
    source.y += subCellOffset.y;

    if (const auto port = runtimeOpenTransportMuzzle(state, attacker, profile)) {
        const auto& host = *port->host;
        const auto forward = runtimeWorldForwardForFacing(std::max(0, host.facing));
        source.x = runtimeObjectRenderX(host) + (forward[0] * port->flh.forwardLeptons
            - forward[1] * port->flh.lateralLeptons) / 256.0;
        source.y = runtimeObjectRenderY(host) + (forward[1] * port->flh.forwardLeptons
            + forward[0] * port->flh.lateralLeptons) / 256.0;
        source.z = runtimeDrawLevelForObject(host) + runtimeWeaponHeightInLevels(port->flh.heightLeptons);
        source.screenOffsetY = -runtimeAircraftSpriteLiftPixels(host, host.airborne);
        source.nativeFixedY = source.screenOffsetY;
        return source;
    }
    if (profile.occupied && attacker.kind == WorldObjectKind::Structure) {
        if (const ArtType* art = runtimeArtTypeForObject(state, attacker);
            art && profile.occupantMuzzleIndex >= 0
            && profile.occupantMuzzleIndex < static_cast<int>(art->muzzleFlashes.size())) {
            const auto offset = art->muzzleFlashes[profile.occupantMuzzleIndex];
            // ART uses 60x30 screen pixels around the building's SHP origin.
            // Store the inverse projection in world coordinates so camera zoom,
            // scrolling and snapshot reconstruction keep the same window.
            source.x += std::trunc((offset.x / 60.0 + offset.y / 30.0) * 256.0) / 256.0;
            source.y += std::trunc((offset.y / 30.0 - offset.x / 60.0) * 256.0) / 256.0;
        }
        return source; // Occupied windows replace ordinary FLH and turret offsets.
    }

    if (const BuildingType* building =
            findRuntimeBuildingType(state.rules, attacker)) {
        if (runtimeSessionGeometryAvailable(state) && !trimAsciiLocal(building->turretAnim).empty()) {
            const RuntimeStructureFootprint footprint =
                runtimeStructureFootprintForObject(state, attacker);
            const RuntimeInputPixelPoint turretOffset =
                state.geometry->runtimeStructureTurretAnimScreenOffsetForRender(
                    *building,
                    attacker.type,
                    footprint.width,
                    footprint.height,
                    RuntimeMapCamera{}
                );
            source.screenOffsetX = turretOffset.x;
            source.screenOffsetY = turretOffset.y;
            // Structure turret voxels still pass through the foundation SHP
            // anchor. Include that half tile in the gattling muzzle origin.
            if (toLowerAsciiLocal(attacker.type) == "yaggun"
                || toLowerAsciiLocal(building->turretAnim) == "yaggun"
                || toLowerAsciiLocal(attacker.type) == "naflak"
                || toLowerAsciiLocal(building->turretAnim) == "flaktur")
                source.screenOffsetY += 15;
        }
    }

    const ArtType* art = runtimeArtTypeForObject(state, attacker);
    std::optional<RuntimeWeaponFireFlh> flh =
        runtimeWeaponFireFlh(state, attacker, profile, art);
    if (!flh.has_value()) {
        return source;
    }
    if (runtimeCachedCapabilityProfileForObject(state, attacker).gattling) {
        const auto burst = state.combatDetails.bursts.find(attacker.instanceId);
        if (burst != state.combatDetails.bursts.end() && profile.weapon
            && burst->second.weaponId == profile.weapon->id
            && burst->second.nextShotIndex % 2 != 0)
            flh->lateralLeptons = -flh->lateralLeptons;
    }

    constexpr double LeptonsPerCell = 256.0;
    int turretOffsetLeptons = 0;
    if (art != nullptr
        && runtimeObjectHasTurret(state, attacker)
        && parseRuntimeInt(art->turretOffset, turretOffsetLeptons)
        && turretOffsetLeptons != 0) {
        // Primary/Secondary/WeaponX FLH is authored relative to the turret
        // center. TurretOffset moves that center along the hull centerline,
        // so it must be applied using the body facing before the muzzle is
        // rotated using the independently turning turret facing.
        const std::array<double, 2> bodyForward =
            runtimeWorldForwardForFacing(
                attacker.facing >= 0 ? attacker.facing : 0
            );
        source.x += bodyForward[0]
            * static_cast<double>(turretOffsetLeptons)
            / LeptonsPerCell;
        source.y += bodyForward[1]
            * static_cast<double>(turretOffsetLeptons)
            / LeptonsPerCell;
    }

    // A structure without a rotating turret has a fixed SHP muzzle. Aiming
    // at another target must not orbit that muzzle around its foundation.
    const int muzzleFacing = attacker.kind == WorldObjectKind::Structure
            && !runtimeObjectHasTurret(state, attacker)
        ? 0 : firingFacing;
    const std::array<double, 2> forward =
        runtimeWorldForwardForFacing(muzzleFacing >= 0 ? muzzleFacing : 0);
    const std::array<double, 2> lateral{-forward[1], forward[0]};
    source.x += (
        forward[0] * static_cast<double>(flh->forwardLeptons)
        + lateral[0] * static_cast<double>(flh->lateralLeptons)
    ) / LeptonsPerCell;
    source.y += (
        forward[1] * static_cast<double>(flh->forwardLeptons)
        + lateral[1] * static_cast<double>(flh->lateralLeptons)
    ) / LeptonsPerCell;
    // Infantry's cropped SHP uses the existing body-relative muzzle mapping.
    // Structures and voxel vehicles use the authored world-height projection.
    source.z += attacker.kind == WorldObjectKind::Infantry
        ? static_cast<double>(flh->heightLeptons) / LeptonsPerCell
        : runtimeWeaponHeightInLevels(flh->heightLeptons);
    return source;
}

RuntimeProjectileSourcePosition runtimeProjectileSourcePosition(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker,
    RuntimeWeaponSlot slot,
    int firingFacing) {
    RuntimeWeaponProfile profile;
    profile.slot = slot;
    return runtimeProjectileSourcePosition(
        state,
        attacker,
        profile,
        firingFacing
    );
}

const SpawnedMissileProfile* runtimeSpawnedMissileProfileForWeapon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WeaponType& weapon) {
    if (!weapon.spawner) {
        return nullptr;
    }
    const UnitType* unit = findRuntimeUnitType(state.rules, attacker);
    if (unit == nullptr || trimAsciiLocal(unit->spawns).empty()) {
        return nullptr;
    }
    return state.rules.findSpawnedMissileProfile(unit->spawns);
}

std::string runtimeWeaponAnimForFacing(
    const WeaponType& weapon,
    int firingFacing) {
    const std::vector<std::string> animationIds =
        splitRuntimeTokenList(weapon.anim);
    if (animationIds.empty()) {
        return {};
    }

    std::string animationId = animationIds.front();
    if (animationIds.size() == 8U) {
        animationId = animationIds[static_cast<std::size_t>(
            worldObjectArtFacingBucket(firingFacing, 8)
        )];
    }
    const auto normalized = toLowerAsciiLocal(trimAsciiLocal(animationId));
    if (normalized == "none" || normalized == "<none>") {
        return {};
    }
    return animationId;
}

double runtimeEffectiveWeaponRange(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    const WeaponType& weapon) {
    if (runtimeSpawnerHasInfiniteRange(weapon)) {
        return std::numeric_limits<double>::infinity();
    }
    // Retail 0x6F7261 checks the target's IsInAir before adding this value.
    const double baseRange = std::max(0.0, weapon.range + (target.airborne
        ? runtimeCachedCapabilityProfileForObject(state, attacker).airRangeBonus : 0.0)
        + (attacker.tankBunkerHostId >= 0
            ? state.rules.getSpecialMechanismRules().bunkerWeaponRangeBonus : 0.0));
    const ProjectileType* projectile =
        findRuntimeProjectileType(state.rules, weapon.projectile);
    // Tracking projectiles continuously correct their path and explicitly
    // ignore the classic elevation range model.
    if (projectile == nullptr
        || !projectile->subjectToElevation
        || projectile->rot != 0
        || attacker.airborne
        || target.airborne
        || attacker.level <= target.level) {
        return baseRange;
    }

    const ElevationModelRules& model =
        state.rules.getElevationModelRules();
    const double height = static_cast<double>(
        attacker.level - target.level
    );
    const double bonusCount = std::min(
        std::max(0.0, model.bonusCap),
        std::floor(height / static_cast<double>(std::max(1, model.increment)))
    );
    return baseRange + bonusCount * std::max(0.0, model.incrementBonus);
}

double runtimeWeaponDistanceToTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    const WeaponType& weapon) {
    if (weapon.cellRangefinding) {
        return runtimeCellDistance(
            runtimeCellPointForObject(attacker),
            runtimeCellPointForObject(target)
        );
    }
    return runtimeCellDistanceToObjectFootprint(
        state,
        runtimeCellPointForObject(attacker),
        target
    );
}

int runtimeWeaponPrefireDelayTicks(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WeaponType& weapon) {
    const ArtType* art = runtimeArtTypeForObject(state, attacker);
    const bool delayedByArt = art != nullptr && art->isAnimDelayedFire;
    if (!weapon.charges && !delayedByArt) {
        return 0;
    }

    int authoredFrames = delayedByArt ? art->delayedFireDelay : 0;
    if (authoredFrames <= 0 && delayedByArt) {
        authoredFrames = std::max(
            runtimeArtSequenceFrameCount(art, "Special"),
            runtimeArtSequenceFrameCount(art, "Fire")
        );
    }
    if (authoredFrames <= 0) {
        authoredFrames = 8;
    }
    return runtimeAuthoredFramesToFixedTicks(state, authoredFrames);
}

void spawnRuntimeWeaponMuzzleEffect(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    bool occupied,
    bool openTopped) {
    // Retail TechnoClass's death-weapon path (0x70D690) constructs a
    // bullet and calls Detonate directly; it never enters the Fire Anim
    // path. OilExplosion's RING1 is therefore not a death effect. Keep
    // ordinary discharges and child projectiles on their existing path.
    if (projectile.weaponAnimId.empty()
        || (projectile.deathWeaponRequestId > 0 && projectile.generation == 0)) {
        return;
    }
    // Squid tentacles belong to the attached host and live for the parasite
    // relationship. They are not an eight-tick flash at the launch position.
    if (toUpperAsciiLocal(projectile.weaponAnimId).rfind("SQDG", 0) == 0) {
        return;
    }

    const auto* muzzleSource = findObjectById(state, projectile.attackerObjectId);
    const bool gattling = !occupied && !openTopped && muzzleSource
        && runtimeCachedCapabilityProfileForObject(state, *muzzleSource).gattling;
    const int muzzleLifetimeTicks = runtimeRuleFramesToTicks(4, state.config.fixedTickHz);
    RuntimeImpactEffect muzzle;
    muzzle.projectileActorId = projectile.actorId;
    muzzle.attackerObjectId = projectile.attackerObjectId;
    muzzle.targetObjectId = projectile.targetObjectId;
    muzzle.owner = projectile.owner;
    muzzle.cell = projectile.fromCell;
    muzzle.level = projectile.fromLevel;
    muzzle.hasWorldPosition = true;
    muzzle.worldX = projectile.sourceX;
    muzzle.worldY = projectile.sourceY;
    muzzle.worldLevel = projectile.sourceZ;
    muzzle.screenOffsetX = projectile.sourceScreenOffsetX;
    muzzle.nativeFixedX = projectile.sourceNativeFixedX;
    muzzle.screenOffsetY = projectile.sourceScreenOffsetY;
    muzzle.nativeFixedY = projectile.sourceNativeFixedY;
    muzzle.totalTicks = muzzleLifetimeTicks;
    muzzle.remainingTicks = muzzleLifetimeTicks;
    muzzle.frameIndex = -1;
    muzzle.facing = projectile.attackerFacing;
    muzzle.lifecycleState = openTopped ? "TransportMuzzle" : occupied ? "OccupantMuzzle"
        : gattling ? "GattlingMuzzle" : "Muzzle";
    muzzle.renderIntent = "weaponAnim";
    muzzle.weaponId = projectile.weaponId;
    muzzle.weaponSlot = projectile.weaponSlot;
    muzzle.projectileId = projectile.projectileId;
    muzzle.projectileImageId = projectile.projectileImageId;
    muzzle.animId = projectile.weaponAnimId;
    muzzle.warheadId = projectile.warheadId;
    muzzle.bright = projectile.bright;
    const WarheadType* muzzleWarhead =
        findRuntimeWarheadType(state.rules, projectile.warheadId);
    if (muzzleWarhead != nullptr && muzzleWarhead->mindControl) {
        // Do not squeeze YURICNTL into the generic eight-tick muzzle flash.
        initializeRuntimeImpactAnimationTimeline(state, muzzle, true);
    }
    if (openTopped) {
        const auto anchor = runtimeTransportMuzzleAnchor(state, projectile.attackerObjectId);
        if (!anchor) return;
        muzzle.muzzleAnchorX = (*anchor)[0]; muzzle.muzzleAnchorY = (*anchor)[1];
        muzzle.muzzleAnchorLevel = (*anchor)[2]; muzzle.muzzleFacing = projectile.attackerFacing;
        // Keep the authoritative authored port untouched. The saved pixel
        // correction is presentation-only, in the fixed 60x30 reference view.
    }
    if (occupied || openTopped || gattling) {
        initializeRuntimeImpactAnimationTimeline(state, muzzle, true);
        if (muzzle.animationSequenceSteps > 0) {
            const ArtType* art = state.art.findArtType(muzzle.animId);
            // Retail AnimType defaults to one logic frame. A positive Rate is
            // converted with integer 900 / Rate, not the ordinary flash's 8 ticks.
            int authoredDelay = art && art->rate > 0 ? std::max(1, 900 / art->rate) : 1;
            if (gattling && art && art->normalized) {
                // Retail AnimType defaults to delay 1 and Shadow=false.
                // VTMUZZLE uses GameOptions::GetAnimSpeed (0x5FB2E0).
                authoredDelay = runtimeRuleNormalizedAnimationDelay(authoredDelay,
                    6 - std::clamp(state.config.gameSpeed, 0, 6));
            }
            muzzle.animationFrameDelayTicks = std::max(1,
                (authoredDelay * std::clamp(state.config.fixedTickHz, 1, 1000) + 14) / 15);
            // The creation pass must display frame zero before advancing.
            muzzle.totalTicks = muzzle.animationSequenceSteps * muzzle.animationFrameDelayTicks + 1;
            muzzle.remainingTicks = muzzle.totalTicks;
        }
    }
    state.impactEffects.push_back(std::move(muzzle));

    RuntimeParticleSpawnContext particleContext;
    particleContext.sourceObjectId = projectile.attackerObjectId;
    particleContext.owner = projectile.owner;
    particleContext.sourceTag = "animation:" + projectile.weaponAnimId;
    particleContext.position = {
        projectile.sourceX * 256.0,
        projectile.sourceY * 256.0,
        projectile.sourceZ * 256.0
    };
    particleContext.direction = {
        (projectile.targetX - projectile.sourceX) * 256.0,
        (projectile.targetY - projectile.sourceY) * 256.0,
        (projectile.targetZ - projectile.sourceZ) * 256.0
    };
    particleContext.seed =
        (static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(projectile.actorId)
         ) << 32U)
        ^ static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(state.tick)
          )
        ^ 0x6d757a7a6c65ULL;
    (void)spawnRuntimeArtAnimationParticles(
        state,
        projectile.weaponAnimId,
        particleContext
    );
}

void spawnRuntimeWeaponAttachedParticleSystem(
    RuntimeGameSessionState& state,
    const WeaponType& weapon,
    const RuntimeProjectileEffect& projectile) {
    const bool requestsAttachedParticles = weapon.isRailgun
        || weapon.useSparkParticles
        || weapon.useFireParticles;
    const std::string particleSystemId =
        trimAsciiLocal(weapon.attachedParticleSystem);
    if (!requestsAttachedParticles || particleSystemId.empty()) {
        return;
    }

    const double sourceX = projectile.hasSourcePosition
        ? projectile.sourceX
        : static_cast<double>(projectile.fromCell.x);
    const double sourceY = projectile.hasSourcePosition
        ? projectile.sourceY
        : static_cast<double>(projectile.fromCell.y);
    const double sourceZ = projectile.hasSourcePosition
        ? projectile.sourceZ
        : static_cast<double>(projectile.fromLevel);
    const double targetX = projectile.hasTargetPosition
        ? projectile.targetX
        : static_cast<double>(projectile.toCell.x);
    const double targetY = projectile.hasTargetPosition
        ? projectile.targetY
        : static_cast<double>(projectile.toCell.y);
    const double targetZ = projectile.hasTargetPosition
        ? projectile.targetZ
        : static_cast<double>(projectile.toLevel);

    RuntimeParticleSpawnContext particleContext;
    particleContext.sourceObjectId = projectile.attackerObjectId;
    particleContext.owner = projectile.owner;
    particleContext.sourceTag = "weapon:" + weapon.id;
    const bool targetAnchoredSpark = weapon.useSparkParticles
        && !weapon.isRailgun
        && !weapon.useFireParticles;
    particleContext.position = {
        (targetAnchoredSpark ? targetX : sourceX) * 256.0,
        (targetAnchoredSpark ? targetY : sourceY) * 256.0,
        (targetAnchoredSpark ? targetZ : sourceZ) * 256.0
    };
    particleContext.direction = {
        (targetX - sourceX) * 256.0,
        (targetY - sourceY) * 256.0,
        (targetZ - sourceZ) * 256.0
    };
    particleContext.seed = runtimeStableHashString(
        "runtimeWeaponAttachedParticleSystem/1"
    );
    runtimeStableHashMix(particleContext.seed, projectile.actorId);
    runtimeStableHashMix(
        particleContext.seed,
        projectile.attackerObjectId
    );
    runtimeStableHashMix(particleContext.seed, projectile.spawnTick);
    runtimeStableHashMixString(particleContext.seed, weapon.id);

    const ParticleSystemType* attachedType =
        state.rules.findParticleSystemType(particleSystemId);
    if (!weapon.isRailgun
        && attachedType != nullptr
        && attachedType->lifetime <= 0) {
        // Authored Spark systems such as WeldingSys intentionally omit a
        // lifetime because the weapon owns them. Bound that ownership to one
        // ROF window so repeated shots cannot leave immortal emitters behind.
        particleContext.systemLifetimeOverrideFrames = std::max(
            1,
            weapon.rof
        );
    }

    ensureRuntimeParticlesInitialized(state);
    (void)state.particles.spawnSystem(
        state.rules,
        particleSystemId,
        particleContext
    );
}

long long runtimeCombatProjectileAuthoredDamage(const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile, const WeaponType& weapon) {
    int rounds = 1;
    // Cluster fragments, shrapnel and spawned-missile terminal weapons have
    // their own actor counts. Their virtual weapon burst remains aggregate.
    if (projectile.parentActorId >= 0 && projectile.generation > 0)
        rounds = std::max(1, weapon.burst);
    else if (const auto found = state.combatDetails.legacyProjectileRounds.find(projectile.actorId);
        found != state.combatDetails.legacyProjectileRounds.end()) rounds = found->second;
    return static_cast<long long>(weapon.damage) * rounds;
}

void prepareRuntimeCombatProjectile(
    RuntimeGameSessionState& state, RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& attacker, const WorldObjectInstance& target,
    const WeaponType& weapon, const ProjectileType* projectileType) {
    if (projectile.spawnedMissileDamage) return;
    const auto& detailRules = state.rules.getCombatDetailRules();
    if (projectileType != nullptr && (projectileType->inaccurate || projectileType->flakScatter)
        && !projectileType->vertical && !projectileType->dropping
        && !weapon.areaFire && !weapon.laser && !weapon.electricBolt) {
        const auto seed = runtimeCombatRandomMix(
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(projectile.actorId))
            ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.tick)) << 32U)
            ^ static_cast<std::uint32_t>(attacker.instanceId)
            ^ runtimeCombatRandomMix(static_cast<std::uint64_t>(state.config.simulationRandomSeed)));
        const auto offset = runtimeCombatScatterOffset(seed, detailRules.ballisticScatter);
        projectile.targetX += static_cast<double>(offset.xLeptons) / 256.0;
        projectile.targetY += static_cast<double>(offset.yLeptons) / 256.0;
        projectile.toCell = {static_cast<int>(std::lround(projectile.targetX)),
            static_cast<int>(std::lround(projectile.targetY))};
        // An inaccurate round aims at this immutable world point. Retaining
        // targetObjectId would make syncRuntimeProjectileTargetEndpoint undo
        // the scatter and guarantee a direct hit even outside CellSpread.
        projectile.targetObjectId = -1;
    }
    const double speedMultiplier = runtimeCombatProjectileSpeedMultiplier(
        weapon.turboBoost, runtimeObjectIsAirTarget(state, target), detailRules.turboBoost);
    if (speedMultiplier != 1.0 && !weapon.laser && !weapon.electricBolt && !weapon.diskLaser) {
        projectile.totalTicks = std::clamp(static_cast<int>(std::ceil(
            static_cast<double>(projectile.totalTicks) / speedMultiplier)), 1, 9600);
        projectile.remainingTicks = projectile.totalTicks;
        projectile.impactTick = static_cast<int>(std::min<std::int64_t>(
            std::numeric_limits<int>::max(), static_cast<std::int64_t>(state.tick) + projectile.totalTicks));
        projectile.speedCellsPerTick *= speedMultiplier;
    }
    const UnitType* unit = findRuntimeUnitType(state.rules, attacker);
    if (unit != nullptr && unit->radialFireSegments > 0 && projectile.parentActorId < 0
        && projectileType != nullptr && projectileType->rot > 0
        && !weapon.laser && !weapon.electricBolt) {
        auto& cursor = state.combatDetails.fireCursors[attacker.instanceId];
        projectile.attackerFacing = runtimeCombatRadialFacing(
            attacker.facing, unit->radialFireSegments, cursor.radialSegment);
        cursor.radialSegment = (cursor.radialSegment + 1) % std::clamp(unit->radialFireSegments, 1, 128);
        // renderIntent and attackerFacing are already part of save, snapshot
        // and authoritative projectile hashes. The heading survives reload.
        projectile.renderIntent = "radialProjectile";
    }
}

bool advanceRuntimeRadialProjectilePosition(const RuntimeGameSessionState& state, RuntimeProjectileEffect& projectile) {
    const auto* type = findRuntimeProjectileType(state.rules, projectile.projectileId);
    if (!type) return false;
    const bool reached = advanceRuntimeHomingMissilePosition(state, projectile, *type);
    const int facing = runtimeFacingFromDelta(
        static_cast<int>(std::lround(std::cos(projectile.homingYaw) * 32767.0)),
        static_cast<int>(std::lround(std::sin(projectile.homingYaw) * 32767.0)));
    if (facing >= 0) projectile.attackerFacing = facing;
    projectile.progress = runtimeProjectileProgress(projectile);
    projectile.currentLevel = static_cast<int>(std::lround(projectile.currentZ));
    projectile.currentCell = {static_cast<int>(std::lround(projectile.currentX)),
        static_cast<int>(std::lround(projectile.currentY))};
    return reached;
}

RuntimeSonicWaveGeometry runtimeSonicWaveGeometry(const RuntimeProjectileEffect& wave) {
    // Convert centred engine cells to the retail integer lepton coordinates.
    const auto point=[](double x,double y,double z) {
        return RuntimeSonicWavePoint{std::trunc((x+0.5)*256.0),
            std::trunc((y+0.5)*256.0),std::trunc(z*256.0/std::sqrt(6.0))};
    };
    RuntimeSonicWaveGeometry g;
    g.source=point(wave.sourceX,wave.sourceY,wave.sourceZ);
    g.target=point(wave.targetX,wave.targetY,wave.targetZ);
    const double dx=g.target.x-g.source.x,dy=g.target.y-g.source.y;
    const double distance=std::hypot(dx,dy);
    if(distance<1.0) return g;
    const double length=std::trunc(distance);
    const float ux=static_cast<float>(dx/distance),uy=static_cast<float>(dy/distance);
    const auto edge=[&](double forward,double sideways) {
        const float x=static_cast<float>(forward),y=static_cast<float>(sideways);
        return RuntimeSonicWavePoint{std::trunc(g.source.x+static_cast<float>(x*ux-y*uy)),
            std::trunc(g.source.y+static_cast<float>(x*uy+y*ux)),g.source.z};
    };
    // Sonic mode's original rotated vertices: (+30,+/-100) at the source,
    // (length-30,+/-100) at the destination (0x761640 and 0x761BC7).
    g.lowerFrom=edge(30.0,-100.0);g.lowerTo=edge(length-30.0,-100.0);
    g.upperFrom=edge(30.0,100.0);g.upperTo=edge(length-30.0,100.0);
    return g;
}

int runtimeSonicWaveInitialDirection(const RuntimeProjectileEffect& wave) {
    const auto g=runtimeSonicWaveGeometry(wave);
    // 0x75F230 chooses an eight-way direction from the projected source to
    // the upper destination vertex. It is stored once in the constructor.
    const double dx=g.upperTo.x-g.source.x,dy=g.upperTo.y-g.source.y;
    const double screenX=dx-dy,screenY=(dx+dy)*0.5;
    constexpr double pi=3.14159265358979323846;
    const double angle=std::atan2(screenX,-screenY);
    return (static_cast<int>(std::floor(angle/(pi/4.0)+0.5))+8)%8;
}

std::vector<int> runtimeSonicWaveDamageCells(const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& wave,double tail) {
    constexpr double step=static_cast<double>(0.05f);
    const auto g=runtimeSonicWaveGeometry(wave);
    const auto key=[](const RuntimeSonicWavePoint& p) {
        return runtimeCellKey(static_cast<int>(p.x)/256,static_cast<int>(p.y)/256);
    };
    const auto interpolate=[](const RuntimeSonicWavePoint& a,const RuntimeSonicWavePoint& b,double t) {
        return RuntimeSonicWavePoint{std::trunc(a.x*(1.0-t)+b.x*t),
            std::trunc(a.y*(1.0-t)+b.y*t),std::trunc(a.z*(1.0-t)+b.z*t)};
    };
    std::vector<int> cells;
    std::set<int> seen;
    // Preserve insertion order: reactions and deaths may affect later cells.
    const auto add=[&](int cell) {
        if(state.terrainByCell.contains(cell) && seen.insert(cell).second) cells.push_back(cell);
    };
    const bool tracking=wave.speedCellsPerTick>0.0;
    int previousCenter=key(tracking?g.source:g.target);
    const auto& edgeFrom=wave.rot<4?g.lowerFrom:g.upperFrom;
    const auto& edgeTo=wave.rot<4?g.lowerTo:g.upperTo;
    int previousEdge=key(edgeTo);
    for(double t=tracking?step:tail;t<=wave.progress+0.5*step+1e-9;t+=step) {
        if(!tracking) {
            // Retail samples one side as well as the centre after release;
            // it does not fill the entire rectangle or apply a blast radius.
            const double edgeT=wave.rot<4?t:static_cast<double>(static_cast<float>(t));
            const int cell=key(interpolate(edgeFrom,edgeTo,edgeT));
            if(cell!=previousEdge) {add(cell);previousEdge=cell;}
        }
        const int cell=key(interpolate(g.source,g.target,t));
        if(cell!=previousCenter) {add(cell);previousCenter=cell;}
    }
    return cells;
}

void spawnRuntimeSonicWave(RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& shot, const WeaponType& weapon) {
    if (!weapon.sonic || shot.deathWeaponCascadeId != 0 || shot.airstrikeStrikeId != 0) return;
    RuntimeProjectileEffect wave = shot;
    wave.actorId = nextRuntimeProjectileActorId(state);
    wave.parentActorId = -1;
    wave.renderIntent = "sonicWave";
    wave.projectileImageId.clear(); wave.impactAnimId.clear(); wave.trailerAnimId.clear();
    wave.particleId.clear(); wave.laser = false; wave.electricBolt = false;
    wave.shrapnel = wave.shrapnelReleased = false;
    wave.totalTicks = std::max(1, (101 * std::max(1, state.config.fixedTickHz) + 14) / 15);
    wave.remainingTicks = wave.totalTicks; wave.elapsedTicks = 0;
    wave.impactTick = wave.spawnTick + wave.totalTicks;
    wave.progress = static_cast<double>(0.05f); // retail ctor's first Update_Wave
    const double distance = std::hypot(wave.targetX - wave.sourceX, wave.targetY - wave.sourceY);
    // Retail ctor rejects rays shorter than 240 leptons (0x75EA83).
    if (distance < 240.0/256.0) return;
    wave.targetZ += runtimeWeaponHeightInLevels(50);
    wave.rot = runtimeSonicWaveInitialDirection(wave);
    wave.speedCellsPerTick = distance * static_cast<double>(0.05f);
    wave.currentX = wave.sourceX; wave.currentY = wave.sourceY; wave.currentZ = wave.sourceZ;
    wave.currentCell = wave.fromCell; wave.currentLevel = wave.fromLevel;
    state.projectiles.push_back(std::move(wave));
    recordRuntimeProjectileReplayEvent(state, state.projectiles.back(), "ProjectileSpawned");
}

void notifyRuntimeWeaponFired(RuntimeProductionHostState& state, const WorldObjectInstance& attacker,
    const WorldObjectInstance& target, const WeaponType& weapon) {
    if (!state.visionEnabled) return;
    const auto previous = state.vision.revisionFor(state.economy.owner);
    state.vision.notifyWeaponFired(attacker, target, weapon, state.tick, state.config.fixedTickHz);
    if (state.vision.revisionFor(state.economy.owner) != previous) {
        state.radarTerrainKey.clear();
        state.projectedObjectsCacheValid = false;
        state.repaintRequested = true;
    }
}

bool detonateRuntimeNuclearDeathWeapon(
    RuntimeProductionHostState& state,
    RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& source) {
    if (!runtimeProjectileIsNuclearDeathDetonation(projectile)
        || projectile.parentActorId >= 0 || projectile.generation != 0) {
        return false;
    }
    const auto center = runtimeProjectileTargetPosition(state, source);
    projectile.targetObjectId = -1;
    projectile.sourceAnchorObjectId = -1;
    projectile.fromCell = projectile.toCell = projectile.currentCell = center.cell;
    projectile.fromLevel = projectile.toLevel = projectile.currentLevel = center.level;
    projectile.hasSourcePosition = projectile.hasTargetPosition = true;
    projectile.sourceX = projectile.targetX = projectile.currentX = center.x;
    projectile.sourceY = projectile.targetY = projectile.currentY = center.y;
    projectile.sourceZ = projectile.targetZ = projectile.currentZ = center.level;
    projectile.sourceScreenOffsetX = projectile.sourceScreenOffsetY = 0;
    projectile.sourceNativeFixedX = projectile.sourceNativeFixedY = 0;
    projectile.affectsAllies = projectile.affectsEnemies = true;
    projectile.spawnTick = projectile.impactTick = state.tick;
    projectile.speedCellsPerTick = 0.0;
    projectile.totalTicks = projectile.remainingTicks = 1;
    projectile.elapsedTicks = 0;
    recordRuntimeProjectileReplayEvent(state, projectile, "ProjectileSpawned");

    // This value exists only for the synchronous impact. No traveling or
    // expired missile is retained in saves/snapshots or projected on screen.
    projectile.remainingTicks = 0;
    projectile.elapsedTicks = 1;
    projectile.progress = 1.0;
    projectile.lifecycle = RuntimeProjectileLifecycleState::Impacting;
    projectile.lifecycleState = runtimeProjectileLifecycleName(projectile.lifecycle);
    (void)applyRuntimeProjectileImpact(state, projectile);
    projectile.lifecycle = RuntimeProjectileLifecycleState::Expired;
    projectile.lifecycleState = runtimeProjectileLifecycleName(projectile.lifecycle);
    recordRuntimeProjectileReplayEvent(state, projectile, "ProjectileExpired");
    state.actionSummary.projectilesInFlight = static_cast<int>(state.projectiles.size());
    return true;
}

void spawnRuntimeProjectile(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    const WeaponType& weapon,
    const RuntimeWeaponProfile& weaponProfile,
    int damage,
    int parentActorId,
    int generation,
    bool shrapnel,
    int sourceAnchorObjectId,
    double inheritedVeteranDamageMultiplier,
    std::uint64_t deathWeaponCascadeId,
    std::uint64_t deathWeaponRequestId,
    std::uint32_t deathWeaponCascadeDepth,
    bool allowSpawnedMissileProfile,
    const RuntimeProjectileEffect* inheritedCausalProjectile,
    int reservedActorId,
    int airstrikeStrikeId,
    int airstrikePlaneToken,
    bool forceFire) {
    const double veteranDamageMultiplier =
        inheritedVeteranDamageMultiplier >= 0.0
            ? inheritedVeteranDamageMultiplier
            : runtimeVeterancyAbilityMultiplier(
                state,
                attacker,
                VeteranAbility::Firepower
            );
    const RuntimeCellPoint fromCell = runtimeCellPointForObject(attacker);
    RuntimeProjectileTargetPosition targetPosition =
        runtimeProjectileTargetPosition(state, target);
    RuntimeCellPoint toCell = targetPosition.cell;
    double travelDistance = runtimeCellDistance(fromCell, toCell);
    const int authoredTravelTicks = runtimeProjectileTravelTicks(
        state.rules, weapon,
        travelDistance, state.config.fixedTickHz
    );
    int travelTicks = weapon.laser && weapon.laserDuration > 0
        ? std::max(1, runtimeRuleFramesToTicks(weapon.laserDuration, state.config.fixedTickHz))
        : (weapon.electricBolt
            ? std::max(runtimeRuleFramesToTicks(5, state.config.fixedTickHz), authoredTravelTicks)
            : authoredTravelTicks);
    const ProjectileType* projectileType =
        findRuntimeProjectileType(state.rules, weapon.projectile);
    const SpawnedMissileProfile* spawnedMissile =
        allowSpawnedMissileProfile
            ? runtimeSpawnedMissileProfileForWeapon(
                state,
                attacker,
                weapon
              )
            : nullptr;
    const bool eliteSpawner = attacker.veterancy >= 2;
    const std::string effectiveWarheadId = spawnedMissile == nullptr
        ? weapon.warhead
        : (eliteSpawner
            ? spawnedMissile->eliteWarhead
            : spawnedMissile->warhead);
    const WarheadType* warhead =
        findRuntimeWarheadType(state.rules, effectiveWarheadId);
    int spawnedMissileBaseDamage = 0;
    if (spawnedMissile != nullptr) {
        spawnedMissileBaseDamage = eliteSpawner
            ? spawnedMissile->eliteDamage
            : spawnedMissile->damage;
        damage = runtimeAuthoredDamageAgainst(
            state,
            target,
            spawnedMissileBaseDamage,
            effectiveWarheadId,
            0.0,
            veteranDamageMultiplier
        );
    }
    const int firingFacing =
        runtimeWeaponFiringFacing(state, attacker, target);
    RuntimeProjectileSourcePosition source =
        runtimeProjectileSourcePosition(
            state, attacker, weaponProfile, firingFacing
        );
    if (shrapnel && inheritedCausalProjectile != nullptr
        && inheritedCausalProjectile->hasTargetPosition
        && parentActorId == inheritedCausalProjectile->actorId) {
        // Splits start at the parent's exact hit, including movement between
        // cells. The render actor applies the same body attachment at both ends.
        source.x = inheritedCausalProjectile->targetX;
        source.y = inheritedCausalProjectile->targetY;
        source.z = inheritedCausalProjectile->targetZ;
        source.screenOffsetX = source.screenOffsetY = 0;
        source.nativeFixedX = source.nativeFixedY = 0;
    }
    if (!shrapnel) {
        alignRuntimeWeaponMuzzleToSprite(state, attacker, weaponProfile, firingFacing, source);
    }
    const bool dropping = projectileType != nullptr
        && projectileType->dropping;
    if (weapon.areaFire) {
        targetPosition.cell = fromCell;
        targetPosition.x = source.x;
        targetPosition.y = source.y;
        targetPosition.level = terrainLevelAtRuntimeCell(
            state,
            fromCell.x,
            fromCell.y
        );
        toCell = fromCell;
        travelDistance = std::sqrt(
            ((source.z - static_cast<double>(targetPosition.level))
                * (source.z - static_cast<double>(targetPosition.level)))
        );
        travelTicks = std::max(
            1,
            runtimeProjectileTravelTicks(state.rules, weapon, travelDistance, state.config.fixedTickHz)
        );
        // Invisible radial effects have no flight from the head-height FLH to
        // the ground. Resolve through the normal impact stage on the next tick.
        if (projectileType != nullptr && projectileType->inviso
            && (weapon.radLevel > 0 || (warhead != nullptr && warhead->psychicDamage))) {
            travelTicks = 1;
        }
    }
    if (dropping) {
        // TS/RA2/YR resolves Dropping as an immediate barrel detonation. Keep
        // the authored firing coordinate (rather than the original engine's
        // erroneous 0,0,0 reset) and let Airburst consume it next tick.
        targetPosition.cell = fromCell;
        targetPosition.x = source.x;
        targetPosition.y = source.y;
        targetPosition.level = static_cast<int>(std::lround(source.z));
        toCell = fromCell;
        travelDistance = 0.0;
        travelTicks = 1;
    } else if (projectileType != nullptr && projectileType->vertical) {
        if (!projectileType->inviso && !projectileType->imageIsNone
            && !trimAsciiLocal(projectileType->image).empty()) {
            // Visible vertical projectiles fall from the release point. Keep
            // an invisible relationship weapon (e.g. DiskDrain) target-bound.
            targetPosition.x = source.x;
            targetPosition.y = source.y;
            targetPosition.cell = {static_cast<int>(std::lround(source.x)),
                static_cast<int>(std::lround(source.y))};
            targetPosition.level = terrainLevelAtRuntimeCell(state,
                targetPosition.cell.x, targetPosition.cell.y);
            toCell = targetPosition.cell;
        }
        const double dx = targetPosition.x - source.x;
        const double dy = targetPosition.y - source.y;
        const double dz =
            static_cast<double>(targetPosition.level) - source.z;
        travelDistance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
        travelTicks = std::max(
            travelTicks,
            runtimeProjectileTravelTicks(state.rules, weapon, travelDistance, state.config.fixedTickHz)
        );
    }

    if (weapon.diskLaser)
        travelTicks = runtimeAuthoredFramesToFixedTicks(state, runtimeDiskLaserChargeFrames);
    const double authoredTravelSpeed = travelDistance
        / static_cast<double>(std::max(1, travelTicks));
    if (projectileType != nullptr
        && projectileType->ranged
        && !dropping
        && !weapon.laser
        && !weapon.electricBolt) {
        const double fuelDistance = std::max(
            travelDistance,
            std::max(0.0, weapon.range) * 2.0
        );
        travelTicks = std::max(
            travelTicks,
            runtimeProjectileTravelTicks(state.rules, weapon, fuelDistance, state.config.fixedTickHz)
        );
    }

    RuntimeProjectileEffect projectile;
    projectile.actorId = reservedActorId > 0
        ? reservedActorId
        : nextRuntimeProjectileActorId(state);
    const bool validCausalChild = parentActorId > 0
        && parentActorId < projectile.actorId
        && generation > 0
        && generation <= 4;
    projectile.parentActorId = validCausalChild ? parentActorId : -1;
    projectile.generation = validCausalChild ? generation : 0;
    projectile.attackerObjectId = attacker.instanceId;
    const bool verticalBomb = projectileType != nullptr && projectileType->vertical
        && !projectileType->inviso && !projectileType->imageIsNone
        && !trimAsciiLocal(projectileType->image).empty();
    projectile.targetObjectId = weapon.areaFire || dropping || verticalBomb
        ? -1
        : target.instanceId;
    projectile.sourceAnchorObjectId = sourceAnchorObjectId;
    projectile.owner = attacker.owner;
    projectile.fromCell = fromCell;
    projectile.toCell = toCell;
    projectile.currentCell = fromCell;
    projectile.hasSourcePosition = true;
    projectile.sourceX = source.x;
    projectile.sourceY = source.y;
    projectile.sourceZ = source.z;
    projectile.sourceScreenOffsetX = source.screenOffsetX;
    projectile.sourceNativeFixedX = source.nativeFixedX;
    projectile.sourceScreenOffsetY = source.screenOffsetY;
    projectile.sourceNativeFixedY = source.nativeFixedY;
    projectile.fromLevel = static_cast<int>(std::lround(source.z));
    projectile.hasTargetPosition = true;
    projectile.targetX = targetPosition.x;
    projectile.targetY = targetPosition.y;
    projectile.targetZ = static_cast<double>(targetPosition.level);
    projectile.toLevel = targetPosition.level;
    projectile.currentLevel = projectile.fromLevel;
    projectile.attackerFacing = weapon.diskLaser
        ? runtimeDiskLaserFrontIndex(targetPosition.x - source.x, targetPosition.y - source.y) * 16
        : firingFacing;
    projectile.targetFacing =
        target.turretFacing >= 0 ? target.turretFacing : target.facing;
    projectile.spawnTick = state.tick;
    projectile.impactTick = state.tick + travelTicks;
    projectile.elapsedTicks = 0;
    projectile.lifecycle = RuntimeProjectileLifecycleState::Spawned;
    projectile.lifecycleState =
        runtimeProjectileLifecycleName(projectile.lifecycle);
    projectile.renderIntent = weapon.diskLaser ? "diskLaserCharge" : spawnedMissile != nullptr
        ? "spawnedMissile"
        : (projectileType != nullptr && projectileType->vertical
            ? "fallingBomb"
            : (weapon.laser
                ? "laserBeam"
                : (weapon.electricBolt ? "electricBolt" : "projectile")));
    projectile.damage = damage;
    projectile.veteranDamageMultiplier = veteranDamageMultiplier;
    projectile.spawnedMissileDamage = spawnedMissile != nullptr;
    projectile.spawnedMissileBaseDamage = spawnedMissileBaseDamage;
    projectile.totalTicks = travelTicks;
    projectile.remainingTicks = travelTicks;
    projectile.weaponId = weapon.id;
    projectile.weaponSlot = runtimeWeaponSlotName(weaponProfile.slot);
    projectile.projectileId = weapon.projectile;
    projectile.projectileImageId =
        projectileType == nullptr ? "" : projectileType->image;
    if (spawnedMissile != nullptr) {
        projectile.projectileImageId = spawnedMissile->typeId;
    }
    if (toLowerAsciiLocal(projectile.projectileImageId) == "none") {
        projectile.projectileImageId.clear();
    }
    projectile.warheadId = effectiveWarheadId;
    const bool openToppedMuzzle = weaponProfile.openTransport
        && toLowerAsciiLocal(attacker.mission) == "insidetransport";
    projectile.weaponAnimId = weaponProfile.occupied
        ? trimAsciiLocal(weapon.occupantAnim) : runtimeWeaponAnimForFacing(weapon, firingFacing);
    if (weaponProfile.occupied && (toLowerAsciiLocal(projectile.weaponAnimId) == "none"
            || toLowerAsciiLocal(projectile.weaponAnimId) == "<none>"))
        projectile.weaponAnimId.clear();
    if (projectile.weaponAnimId.empty() && openToppedMuzzle) {
        projectile.weaponAnimId = trimAsciiLocal(weapon.openToppedAnim);
        if (toLowerAsciiLocal(projectile.weaponAnimId) == "none"
            || toLowerAsciiLocal(projectile.weaponAnimId) == "<none>") projectile.weaponAnimId.clear();
    }
    projectile.impactAnimId = chooseRuntimeImpactAnim(warhead, weapon);
    projectile.trailerAnimId =
        projectileType == nullptr ? "" : projectileType->trailer;
    projectile.trailerIntentId = projectile.trailerAnimId;
    projectile.warheadImpactIntentId = projectile.warheadId;
    projectile.reportSoundId = weapon.report;
    projectile.arcing = projectileType != nullptr && projectileType->arcing;
    projectile.vertical = projectileType != nullptr
        && projectileType->vertical;
    projectile.acceleration =
        projectileType == nullptr ? 0 : projectileType->acceleration;
    projectile.high = projectileType != nullptr && projectileType->high;
    projectile.veryHigh = projectileType != nullptr && projectileType->veryHigh;
    if (spawnedMissile != nullptr) {
        projectile.high = true;
        projectile.veryHigh = spawnedMissile->altitudeLeptons >= 512;
    }
    projectile.shadow = projectileType != nullptr && projectileType->shadow;
    projectile.animates = projectileType != nullptr && projectileType->animates;
    projectile.aa = projectileType == nullptr || projectileType->aa;
    projectile.ag = projectileType == nullptr || projectileType->ag;
    projectile.cluster = projectileType == nullptr ? 0 : projectileType->cluster;
    projectile.bright = weapon.bright || (warhead != nullptr && warhead->bright);
    projectile.laser = weapon.laser;
    projectile.houseColorLaser = weapon.houseColorLaser;
    projectile.electricBolt = weapon.electricBolt;
    projectile.alternateElectricColor = weapon.alternateElectricColor;
    // Electric bolts have their own blue/pink palette. ChronoBeamColor and
    // MagnaBeamColor belong to different weapon renderers.
    projectile.electricColor = weapon.alternateElectricColor
        ? "255,200,255" : "80,80,255";
    projectile.shrapnel = shrapnel;
    projectile.shrapnelReleased =
        weapon.laser
        && projectileType != nullptr
        && projectileType->shrapnelCount > 0
        && !trimAsciiLocal(projectileType->shrapnelWeapon).empty()
        && projectile.generation < 4;
    projectile.laserDuration = weapon.laserDuration;
    projectile.laserThickness = std::clamp(weapon.laserThickness, 1, 32);
    projectile.cellSpread = warhead == nullptr ? 0.0 : warhead->cellSpread;
    projectile.percentAtMax = warhead == nullptr ? 0.0 : warhead->percentAtMax;
    // Keep forced shots independent of target tracking: FlakScatter clears
    // targetObjectId, but its impact must still damage the aimed friendly side.
    // For a building Ivan attachment this persisted bit carries the explicit
    // force-order permission, rather than the warhead's ordinary splash policy.
    projectile.affectsAllies = warhead != nullptr && warhead->ivanBomb
            && target.kind == WorldObjectKind::Structure
        ? forceFire : forceFire || (warhead != nullptr && warhead->affectsAllies);
    projectile.affectsEnemies = forceFire || warhead == nullptr || warhead->affectsEnemies;
    projectile.particleId = warhead == nullptr ? "" : warhead->particle;
    projectile.progress = 0.0;
    projectile.currentX = projectile.sourceX;
    projectile.currentY = projectile.sourceY;
    projectile.currentZ = projectile.sourceZ;
    if (runtimeProjectileIsParasiteLeap(state, projectile)) {
        syncRuntimeParasiteLeapTargetPose(state, projectile, target);
    }
    projectile.speedCellsPerTick =
        projectileType != nullptr && projectileType->ranged
            ? authoredTravelSpeed
            : (travelTicks <= 0
                ? 0.0
                : (travelDistance / static_cast<double>(travelTicks)));
    projectile.rot = projectileType == nullptr ? 0 : projectileType->rot;
    projectile.deathWeaponCascadeId = deathWeaponCascadeId;
    projectile.deathWeaponRequestId = deathWeaponRequestId;
    projectile.deathWeaponCascadeDepth = deathWeaponCascadeDepth;
    const bool canInheritCausalProjectile = validCausalChild
        && inheritedCausalProjectile != nullptr
        && inheritedCausalProjectile->actorId == projectile.parentActorId
        && inheritedCausalProjectile->attackerObjectId
            == attacker.instanceId
        && runtimeRepairDepotCanonicalOwnerId(
                state,
                inheritedCausalProjectile->owner
            ) == runtimeRepairDepotCanonicalOwnerId(state, attacker.owner)
        && projectile.generation
            == inheritedCausalProjectile->generation + 1;
    if (canInheritCausalProjectile) {
        projectile.launchSourceKind =
            inheritedCausalProjectile->launchSourceKind;
        projectile.launchSourceType =
            inheritedCausalProjectile->launchSourceType;
        projectile.launchSourceAuthoredAircraft =
            inheritedCausalProjectile->launchSourceAuthoredAircraft;
        projectile.launchSourceValidated =
            inheritedCausalProjectile->launchSourceValidated;
        projectile.airstrikeStrikeId =
            inheritedCausalProjectile->airstrikeStrikeId;
        projectile.airstrikePlaneToken =
            inheritedCausalProjectile->airstrikePlaneToken;
        projectile.particleDamagePulse =
            inheritedCausalProjectile->particleDamagePulse;
        projectile.suppressWarheadParticleSpawn =
            inheritedCausalProjectile->suppressWarheadParticleSpawn;
    } else if (attacker.instanceId >= 0
        && attacker.alive
        && !trimAsciiLocal(attacker.owner).empty()
        && !trimAsciiLocal(attacker.type).empty()) {
        projectile.launchSourceKind = static_cast<int>(attacker.kind);
        projectile.launchSourceType = attacker.type;
        projectile.launchSourceAuthoredAircraft =
            attacker.authoredAircraft;
        projectile.launchSourceValidated = true;
    }
    if (!validCausalChild
        && airstrikeStrikeId > 0
        && airstrikePlaneToken > 0) {
        projectile.airstrikeStrikeId = airstrikeStrikeId;
        projectile.airstrikePlaneToken = airstrikePlaneToken;
    }
    if (detonateRuntimeNuclearDeathWeapon(state, projectile, attacker)) return;
    prepareRuntimeCombatProjectile(state, projectile, attacker, target, weapon, projectileType);
    if (projectileType && runtimeUsesMissileStartupAcceleration(state, projectile, *projectileType)) {
        const int delay = runtimeMissileAccelerationStartupDelayTicks(
            projectile.speedCellsPerTick, projectile.acceleration, state.config.fixedTickHz);
        projectile.totalTicks = static_cast<int>(std::min<std::int64_t>(
            std::numeric_limits<int>::max() - std::max(0, state.tick),
            static_cast<std::int64_t>(projectile.totalTicks) + delay));
        projectile.remainingTicks = projectile.totalTicks;
        projectile.impactTick = state.tick + projectile.totalTicks;
    }
    recordRuntimeProjectileReplayEvent(
        state,
        projectile,
        "ProjectileSpawned"
    );
    const RuntimeProjectileEffect spawnedProjectile = projectile;
    state.projectiles.push_back(std::move(projectile));
    spawnRuntimeSonicWave(state, spawnedProjectile, weapon);
    if (parentActorId < 0 && generation == 0 && !shrapnel && deathWeaponCascadeId == 0
        && allowSpawnedMissileProfile && airstrikeStrikeId == 0) {
        notifyRuntimeWeaponFired(state, attacker, target, weapon);
    }
    spawnRuntimeWeaponMuzzleEffect(state, spawnedProjectile, weaponProfile.occupied, openToppedMuzzle);
    spawnRuntimeWeaponAttachedParticleSystem(
        state,
        weapon,
        spawnedProjectile
    );
    state.actionSummary.projectilesInFlight =
        static_cast<int>(state.projectiles.size());
    if (weapon.diskLaser) {
        const auto& chargeSound = state.rules.getSpecialMechanismRules().diskLaserChargeUp;
        if (!chargeSound.empty()) emitRuntimeSound(state, "weapon:" + chargeSound, RuntimeSpatialAudioPosition{spawnedProjectile.sourceX, spawnedProjectile.sourceY, spawnedProjectile.sourceZ});
    } else if (!weapon.report.empty() && !runtimeProjectileIsCrashDeathDetonation(spawnedProjectile)
        && !manageRuntimeGattlingWeaponReport(state, attacker, weapon)) {
        emitRuntimeSound(state, "weapon:" + weapon.report, RuntimeSpatialAudioPosition{spawnedProjectile.sourceX, spawnedProjectile.sourceY, spawnedProjectile.sourceZ});
    }
    if (spawnedProjectile.shrapnelReleased) {
        spawnRuntimeProjectileShrapnel(
            state,
            spawnedProjectile,
            spawnedProjectile.toCell,
            spawnedProjectile.toLevel
        );
        state.actionSummary.projectilesInFlight =
            static_cast<int>(state.projectiles.size());
    }
}

bool runtimeCombatCanAct(const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
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

void cancelRuntimeCombatFire(RuntimeProductionHostState& state, int objectId) {
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

RuntimeWeaponProfile runtimeCombatPassengerWeaponProfile(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const WorldObjectInstance* target, bool forceFire) {
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

bool runtimeCombatTargetInRange(const RuntimeGameSessionState& state,
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

bool runtimeCombatOpportunityCandidatesReference() {
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_OPPORTUNITY_INDEX_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

const WorldObjectInstance* selectRuntimeCombatOpportunityTarget(
    const RuntimeGameSessionState& state, const WorldObjectInstance& pose,
    const WeaponType* fixedWeapon, double rangeBonus, bool distributed,
    int preferredTargetId, bool forceFire, bool passengerFire,
    bool forceReference,
    RuntimeCombatOpportunityOwnerSummary* ownerSummary,
    RuntimeCombatOpportunityCandidates* candidateIndex) {
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

int fireRuntimeCombatRound(RuntimeProductionHostState& state, const WorldObjectInstance& pose,
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

int runtimeNextBurstDelayTicks(const RuntimeGameSessionState& state, const WorldObjectInstance& attacker,
    const WeaponType& weapon, int shotIndex) {
    const auto seed = runtimeCombatRandomMix(static_cast<std::uint32_t>(state.nextProjectileActorId)
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(attacker.instanceId)) << 32U)
        ^ runtimeCombatRandomMix(static_cast<std::uint64_t>(state.config.simulationRandomSeed))
        ^ static_cast<std::uint32_t>(shotIndex) ^ 0x4255525354ULL);
    const int fallback = weapon.burstDelaySpecified ? weapon.burstDelay : 3 + static_cast<int>(seed % 3);
    return runtimeCombatBurstDelayTicks(fallback, weapon.burstDelays, shotIndex, state.config.fixedTickHz);
}

int beginRuntimeCombatBurst(RuntimeProductionHostState& state, const WorldObjectInstance& attacker,
    const WorldObjectInstance& target, const WeaponType& weapon,
    const RuntimeWeaponProfile& profile, double multiplier, int sourceAnchorId,
    bool forceFire, bool independentFire) {
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

bool advanceRuntimeCombatBursts(RuntimeProductionHostState& state) {
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

bool fireRuntimeIndependentCombatWeapon(RuntimeProductionHostState& state,
    WorldObjectInstance& shooter, const WorldObjectInstance* host,
    const WorldObjectInstance& target, const RuntimeWeaponProfile& profile, bool forceFire,
    RuntimeCombatOpportunityCandidates* candidateIndex) {
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

bool advanceRuntimeCombatIndependentFire(RuntimeProductionHostState& state) {
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

bool runtimePanicEligible(const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    if (!object.alive || object.health <= 0 || object.airborne) return false;
    switch (object.kind) {
    case WorldObjectKind::Infantry:
    case WorldObjectKind::Unit:
        break;
    default:
        return false;
    }
    if (runtimeObjectHasDeployedState(state, object)
        || runtimeMissionIsDeploying(object) || runtimeMissionIsUndeploying(object)) return false;
    const auto* type = findRuntimeUnitType(state.rules, object);
    return type != nullptr && type->speed > 0;
}

bool beginRuntimePanic(RuntimeGameSessionState& state, WorldObjectInstance& object,
    bool permanent, int threatX, int threatY) {
    if (!runtimePanicEligible(state, object)) return false;
    if (!permanent) {
        const auto& type = *findRuntimeUnitType(state.rules, object);
        const bool ignoresDamageReaction = type.fearless
            || runtimeVeterancyHasAbility(state, object, VeteranAbility::Fearless);
        if (!type.fraidycat || ignoresDamageReaction) return false;
    }

    RuntimePanicState reaction;
    const auto existing = state.combatDetails.panics.find(object.instanceId);
    if (existing != state.combatDetails.panics.end()) {
        reaction = existing->second;
    } else {
        reaction.owner = object.owner;
        reaction.sourceType = object.type;
        reaction.nextChooseTick = state.tick;
    }
    // Refresh the finite duration without downgrading a script's permanent
    // reaction or resetting its pending-start flag and deterministic walk cursor.
    reaction.permanent |= permanent;
    reaction.ticksRemaining = 5 * std::clamp(state.config.fixedTickHz, 1, 1000);
    reaction.lastTick = state.tick;
    reaction.threatX = threatX;
    reaction.threatY = threatY;
    state.combatDetails.panics.insert_or_assign(object.instanceId, std::move(reaction));
    object.prone = false;
    object.proneTicksRemaining = 0;
    return true;
}

RuntimePanicClockStep advanceRuntimePanicClock(
    RuntimePanicState& panic, int tick, bool suspended) {
    const auto elapsed = std::max<std::int64_t>(0,
        static_cast<std::int64_t>(tick) - panic.lastTick);
    panic.lastTick = tick;
    if (suspended) return RuntimePanicClockStep::Suspended;
    if (panic.permanent) return RuntimePanicClockStep::Active;
    panic.ticksRemaining = static_cast<int>(std::max<std::int64_t>(0,
        static_cast<std::int64_t>(panic.ticksRemaining) - elapsed));
    return panic.ticksRemaining == 0
        ? RuntimePanicClockStep::Expired : RuntimePanicClockStep::Active;
}

bool advanceRuntimePanics(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimePanics", state.tick);
    bool changed = false;
    std::vector<int> ids;
    for (const auto& [id, panic] : state.combatDetails.panics) { (void)panic; ids.push_back(id); }
    for (int id : ids) {
        auto found = state.combatDetails.panics.find(id);
        if (found == state.combatDetails.panics.end()) continue;
        RuntimePanicState panic = found->second;
        auto* object = findMutableObjectById(state, id);
        if (object == nullptr || !runtimePanicEligible(state, *object)
            || object->owner != panic.owner || object->type != panic.sourceType) {
            eraseRuntimeBehavior(state, id);
            changed = true;
            continue;
        }
        const bool suspended = runtimeComputerAiPausedForOwner(state, object->owner)
            || !runtimeObjectHasOperationalPower(state, *object)
            || runtimeObjectDisabledByParasite(state, id)
            || (state.specialMechanismsInitialized
                && (state.specialMechanisms.isTemporalFrozen(id)
                    || state.specialMechanisms.isChronoMovementLocked(id, state.tick)));
        const auto clockStep = advanceRuntimePanicClock(panic, state.tick, suspended);
        if (clockStep == RuntimePanicClockStep::Suspended) {
            state.combatDetails.panics[id] = std::move(panic);
            continue;
        }
        if (clockStep == RuntimePanicClockStep::Expired) {
            eraseRuntimeBehavior(state, id);
            object->mission = "Guard";
            changed = true;
            continue;
        }
        if (panic.pendingStart) {
            eraseRuntimeBehavior(state, id);
            cancelRuntimeCombatFire(state, id);
            panic.pendingStart = false;
            changed = true;
        }
        state.combatDetails.panics[id] = panic;
        object->prone = false; object->proneTicksRemaining = 0;
        object->mission = "Panic";
        if (state.behaviors.contains(id) || state.tick < panic.nextChooseTick) continue;

        // A bounded local walk uses the very same terrain, corner, bridge,
        // occupancy and infantry subcell rules as an ordinary Move. Failure
        // retries twice per second, without queuing unbounded path searches.
        const auto occupied = buildRuntimeOccupancyLookup(state, id);
        const RuntimeCellPoint start = runtimeCellPointForObject(*object);
        const std::uint64_t seed = runtimeCombatRandomMix(
            (static_cast<std::uint64_t>(id) << 32U) ^ panic.choiceSerial++);
        std::vector<RuntimeCellPoint> bestPath;
        double bestScore = -std::numeric_limits<double>::infinity();
        int direction = 0;
        for (const auto& neighbor : runtimeNeighborsOf(start)) {
            const int radius = 2 + static_cast<int>((seed >> (direction * 3)) & 3U);
            const RuntimeCellPoint goal{start.x + (neighbor.x - start.x) * radius,
                start.y + (neighbor.y - start.y) * radius};
            auto path = findRuntimeGreedyPathUntimed(state, *object, start, goal, -1, nullptr, 12, &occupied);
            if (!path.empty()) {
                const auto end = path.back();
                const double away = panic.threatX >= 0 && panic.threatY >= 0
                    ? std::hypot(static_cast<double>(end.x - panic.threatX),
                        static_cast<double>(end.y - panic.threatY)) : 0.0;
                const double score = away + static_cast<double>((seed >> (direction * 7)) & 127U) / 32.0;
                if (score > bestScore) { bestScore = score; bestPath = std::move(path); }
            }
            ++direction;
        }
        panic.nextChooseTick = state.tick + std::max(1, state.config.fixedTickHz / 2);
        state.combatDetails.panics[id] = panic;
        if (!bestPath.empty()) {
            RuntimeObjectBehavior move;
            move.kind = RuntimeBehaviorKind::Move;
            move.state = RuntimeBehaviorState::Queued;
            move.moveTarget = bestPath.back();
            replaceRuntimeBehaviorPath(move, std::move(bestPath));
            state.behaviors[id] = std::move(move);
            changed = true;
        }
    }
    return changed;
}

void finishRuntimePanicMovementLeg(RuntimeProductionHostState& state, int id) {
    const auto found = state.combatDetails.panics.find(id);
    if (found == state.combatDetails.panics.end()) { eraseRuntimeBehavior(state, id); return; }
    const auto panic = found->second;
    eraseRuntimeBehavior(state, id);
    if (auto* object = findMutableObjectById(state, id); object && runtimePanicEligible(state, *object)) {
        state.combatDetails.panics[id] = panic;
        object->mission = "Panic";
    }
}

void syncRuntimeProjectileTargetEndpoint(
    RuntimeProductionHostState& state,
    RuntimeProjectileEffect& projectile) {
    if (projectile.vertical && !projectile.projectileImageId.empty()) {
        // Also normalize in-flight bombs from older saves. Neither the bomber
        // nor its former target may drag a released bomb sideways.
        projectile.targetObjectId = -1;
        projectile.sourceAnchorObjectId = -1;
        projectile.targetX = projectile.hasSourcePosition ? projectile.sourceX : projectile.fromCell.x;
        projectile.targetY = projectile.hasSourcePosition ? projectile.sourceY : projectile.fromCell.y;
        projectile.toCell = {static_cast<int>(std::lround(projectile.targetX)),
            static_cast<int>(std::lround(projectile.targetY))};
        projectile.toLevel = terrainLevelAtRuntimeCell(state, projectile.toCell.x, projectile.toCell.y);
        projectile.targetZ = projectile.toLevel;
        projectile.hasTargetPosition = true;
        return;
    }
    if (projectile.sourceAnchorObjectId >= 0) {
        const auto* sourceAnchor = findObjectById(state, projectile.sourceAnchorObjectId);
        const auto* passenger = findRuntimeObjectByIdIncludingInactive(state, projectile.attackerObjectId);
        if (sourceAnchor && passenger && runtimeCombatValidPassenger(*passenger, *sourceAnchor)) {
            const auto* targetObject = findObjectById(state, projectile.targetObjectId);
            auto profile = runtimeCombatPassengerWeaponProfile(state, *passenger, targetObject);
            profile.openTransport = true;
            const auto pose = runtimeCombatFiringPose(*passenger, sourceAnchor);
            const int facing = targetObject ? runtimeWeaponFiringFacing(state, pose, *targetObject) : pose.facing;
            auto muzzle = runtimeProjectileSourcePosition(state, pose, profile, facing);
            alignRuntimeWeaponMuzzleToSprite(state, pose, profile, facing, muzzle);
            projectile.fromCell = runtimeCellPointForObject(*sourceAnchor);
            projectile.fromLevel = sourceAnchor->level;
            projectile.hasSourcePosition = true;
            projectile.sourceX = muzzle.x; projectile.sourceY = muzzle.y; projectile.sourceZ = muzzle.z;
            projectile.sourceScreenOffsetX = muzzle.screenOffsetX;
            projectile.sourceNativeFixedX = muzzle.nativeFixedX;
            projectile.sourceScreenOffsetY = muzzle.screenOffsetY;
            projectile.sourceNativeFixedY = muzzle.nativeFixedY;
        } else if (sourceAnchor && sourceAnchor->alive) {
            const auto sourcePosition = runtimeProjectileTargetPosition(state, *sourceAnchor);
            projectile.fromCell = sourcePosition.cell;
            projectile.fromLevel = sourcePosition.level;
            projectile.hasSourcePosition = true;
            projectile.sourceX = sourcePosition.x;
            projectile.sourceY = sourcePosition.y;
            projectile.sourceZ = static_cast<double>(sourcePosition.level);
        }
    }
    const WorldObjectInstance* target =
        findObjectById(state, projectile.targetObjectId);
    if (target == nullptr || !target->alive) {
        return;
    }

    const RuntimeProjectileTargetPosition targetPosition =
        runtimeProjectileTargetPosition(state, *target);
    projectile.toCell = targetPosition.cell;
    projectile.hasTargetPosition = true;
    projectile.targetX = targetPosition.x;
    projectile.targetY = targetPosition.y;
    projectile.targetZ = static_cast<double>(targetPosition.level);
    projectile.toLevel = targetPosition.level;
    if (runtimeProjectileIsParasiteLeap(state, projectile)) {
        syncRuntimeParasiteLeapTargetPose(state, projectile, *target);
    }
}

bool advanceRuntimeOmniCrushContacts(RuntimeProductionHostState& state) {
    std::vector<int> sourceIds;
    for (const auto& object : state.world.getObjects())
        if (runtimeObjectUsesOmniCrushApproach(state, object))
            sourceIds.push_back(object.instanceId);
    std::sort(sourceIds.begin(), sourceIds.end());
    bool changed = false;
    for (int sourceId : sourceIds) {
        const auto* source = findObjectById(state, sourceId);
        if (!source || !source->alive) continue;
        const auto found = state.behaviors.find(sourceId);
        const bool approaching = found != state.behaviors.end()
            && runtimeOmniCrushPendingSegment(state, *source, found->second);
        const RuntimeCellPoint destination = approaching
            ? found->second.renderTo : runtimeCellPointForObject(*source);
        const auto result = resolveRuntimeCrushAtDestination(
            state, sourceId, destination, -1, true);
        changed = result == RuntimeCrushStepResult::Crushed || changed;
        // Exit callbacks can add crew, destroy the source, or erase behavior.
        // Neither world pointers nor behavior references survive that call.
        auto* current = findMutableObjectById(state, sourceId);
        auto live = state.behaviors.find(sourceId);
        if (!approaching || !current || !current->alive || live == state.behaviors.end()
            || !runtimeOmniCrushPendingSegment(state, *current, live->second)) continue;
        if (result == RuntimeCrushStepResult::Blocked) {
            changed = cancelRuntimeBlockedOmniCrushApproach(state, *current, live->second) || changed;
            continue;
        }
        const auto scan = runtimeScanCrushTargetsAtCell(state, *current, destination);
        const double dx = runtimeObjectRenderX(*current) - destination.x;
        const double dy = runtimeObjectRenderY(*current) - destination.y;
        if (!scan.blocked && scan.targetIds.empty() && dx * dx + dy * dy < 0.25
            && current->renderOnBridge == runtimeCellIsBridge(state, destination.x, destination.y)) {
            const auto departure = runtimeCellPointForObject(*current);
            setRuntimeObjectCell(state, *current, destination, false);
            updateRuntimeTickOccupancyForMove(state, *current, departure, destination);
            if (live->second.renderMoveTicksRemaining <= 0) {
                clearRuntimeRenderPosition(*current);
                live->second.renderMoveTicksTotal = live->second.renderMoveTicksRemaining = 0;
                live->second.renderArrivalDistanceAfter = -1.0;
                live->second.moveSegmentFrom = {-1, -1};
            }
            changed = true;
        } else if (live->second.moveCooldownTicks <= 0) {
            // A moving victim or newly spawned crew may still be outside the
            // contact radius. Hold the reached body, with a valid positive
            // saved clock, until the cell is genuinely clear or entry cancels.
            auto& move = live->second;
            move.renderFromOffsetX = current->renderX - move.renderFrom.x;
            move.renderFromOffsetY = current->renderY - move.renderFrom.y;
            move.renderFromLevel = current->renderLevel;
            move.renderFromOnBridge = current->renderOnBridge;
            move.renderMoveTicksTotal = move.renderMoveTicksRemaining = move.moveCooldownTicks = 1;
            move.renderArrivalDistanceAfter = -1.0;
        }
    }
    return changed;
}

int runtimeMcvResolvedDeployFacing(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const UnitType& unit) {
    const auto* building = findRuntimeBuildingTypeById(state.rules, unit.deploysInto);
    const int authored = building && building->deployFacing >= 0
        ? building->deployFacing : unit.deployFacing;
    if (authored >= 0) return worldObjectFacingFromMapFacing(std::clamp(authored, 0, 7) * 32);
    // Retail construction-yard MCVs align to the lower-left isometric edge.
    return runtimeUnitTypeIsMcv(state, unit) ? 160 : object.facing;
}

bool requestRuntimeMcvDeployment(RuntimeProductionHostState& state, int objectId,
    bool selectResult, std::optional<RuntimeCellPoint> requestedCell) {
    auto* mcv = findMutableObjectById(state, objectId);
    const auto* unit = mcv ? findRuntimeUnitType(state.rules, *mcv) : nullptr;
    if (!mcv || !unit || !runtimeUnitTypeIsMcv(state, *unit))
        return deployRuntimeMcv(state, objectId, selectResult, requestedCell);
    if (runtimeMissionIsMcvDeployPending(*mcv)) return true;
    if (mcv->airborne || runtimeObjectDisabledByParasite(state, objectId)
        || state.specialMechanisms.isChronoMovementLocked(objectId, state.tick)) return false;
    const auto item = runtimeBuildItemForBuilding(state, unit->deploysInto);
    const RuntimeCellPoint site = requestedCell.value_or(runtimeCellPointForObject(*mcv));
    // Pending moveTarget remains a center anchor through turning and saves.
    if (!item || !runtimeCellCanPlaceBuilding(state, *item,
        runtimePlacementTopLeftForAnchor(site, runtimeStructureFootprintForBuildItem(state, *item)),
        objectId, nullptr, mcv->owner))
        return deployRuntimeMcv(state, objectId, selectResult, requestedCell);
    const int desired = runtimeMcvResolvedDeployFacing(state, *mcv, *unit);
    const auto previous = state.behaviors.find(objectId);
    const bool committed = previous != state.behaviors.end() && previous->second.moveCooldownTicks > 0;
    if (mcv->facing == desired && !committed)
        return deployRuntimeMcv(state, objectId, selectResult, site);
    const WorldObjectInstance oldPose = *mcv;
    RuntimeObjectBehavior pending = committed ? previous->second : RuntimeObjectBehavior{};
    pending.kind = RuntimeBehaviorKind::Move; pending.state = RuntimeBehaviorState::Queued;
    pending.moveTarget = site; pending.targetObjectId = -1; pending.path.clear();
    pending.pathRequestPending = pending.pendingPathTowardRange = pending.pendingHasSharedGoal = false;
    pending.pendingPathStart = pending.pendingPathGoal = pending.pendingSharedGoal = {-1,-1};
    pending.pendingAllowedOccupiedObjectId = pending.pendingTargetObjectId = -1;
    pending.pendingIgnoredObjectIds.clear(); pending.repackMoveGroup.reset();
    pending.autoAcquired = pending.forceFire = pending.attackGround = pending.attackBridge = pending.returnToDeployed = false;
    eraseRuntimeBehavior(state, objectId);
    mcv = findMutableObjectById(state, objectId); if (!mcv) return false;
    *mcv = oldPose; // Erasing an order must not snap the already committed step.
    if (!committed) mcv->bodyFacingClock = {};
    mcv->mission = selectResult ? "McvDeployTurning" : "McvDeployTurningUnselected";
    state.behaviors[objectId] = std::move(pending);
    state.lastCommand.valid = false;
    state.economy.status = state.interactionStatus = "MCV turning to deploy";
    invalidateRuntimeTickOccupancyCache(state); invalidateRuntimeObjectProjectionCache(state);
    return true;
}

bool advanceRuntimeMcvDeployTurns(RuntimeProductionHostState& state) {
    std::vector<int> pendingIds;
    for (const auto& object : state.world.getObjects())
        if (object.alive && runtimeMissionIsMcvDeployPending(object)) pendingIds.push_back(object.instanceId);
    std::sort(pendingIds.begin(), pendingIds.end());
    bool changed = false;
    for (const int id : pendingIds) {
        auto* mcv = findMutableObjectById(state, id);
        const auto* unit = mcv ? findRuntimeUnitType(state.rules, *mcv) : nullptr;
        const auto behavior = state.behaviors.find(id);
        if (!mcv || !unit || !runtimeUnitTypeIsMcv(state, *unit) || behavior == state.behaviors.end()) {
            if (mcv) mcv->mission = "Guard";
            continue;
        }
        if (runtimeObjectDisabledByParasite(state, id) || (state.specialMechanismsInitialized
            && (state.specialMechanisms.isChronoMovementLocked(id, state.tick) || state.specialMechanisms.isTemporalFrozen(id)))) continue;
        auto& pending = behavior->second;
        if (pending.moveCooldownTicks > 0) {
            changed = advanceRuntimeRenderMove(state, *mcv, pending) || changed;
            if (--pending.moveCooldownTicks == 0) {
                pending.moveSegmentFrom = {-1,-1}; clearRuntimeRenderPosition(*mcv);
                mcv->bodyFacingClock = {}; invalidateRuntimeTickOccupancyCache(state);
            }
            continue;
        }
        const auto mission = toLowerAsciiLocal(mcv->mission);
        const bool selectResult = mission == "mcvdeployturning" || mission == "mcvdeployready";
        const bool ready = mission == "mcvdeployready" || mission == "mcvdeployreadyunselected";
        const int desired = runtimeMcvResolvedDeployFacing(state, *mcv, *unit);
        if (!ready) {
            mcv->facing = runtimeAdvanceBodyFacing(state, *mcv, mcv->facing, desired);
            mcv->turretFacing = mcv->facing;
            if (mcv->facing == desired) mcv->mission = selectResult ? "McvDeployReady" : "McvDeployReadyUnselected";
            changed = true;
            continue;
        }
        // Keep the aligned chassis visible for one tick, then atomically
        // transform. Recheck the whole foundation after the turn's delay.
        const RuntimeCellPoint site = pending.moveTarget;
        if (!deployRuntimeMcv(state, id, selectResult, site)) {
            eraseRuntimeBehavior(state, id);
            if (auto* stopped = findMutableObjectById(state, id)) stopped->mission = "Guard";
            emitRuntimeSound(state, runtimeSoundIntentCannotDeployHere);
        }
        changed = true;
    }
    return changed;
}

bool advanceRuntimeStructureRepacks(RuntimeProductionHostState& state) {
    std::vector<int> completed;
    for (const auto& object : state.world.getObjects()) {
        if (runtimeStructureIsRepacking(object) && object.buildAnimTicksRemaining <= 0
            && !(state.specialMechanismsInitialized
                && state.specialMechanisms.isTemporalFrozen(object.instanceId))) {
            completed.push_back(object.instanceId);
        }
    }
    bool changed = false;
    for (int id : completed) {
        const bool selected = std::find(state.selectedObjectIds.begin(),
            state.selectedObjectIds.end(), id) != state.selectedObjectIds.end();
        std::optional<RuntimeCellPoint> moveTarget;
        std::optional<RuntimeRepackMoveGroupIntent> moveGroup;
        const auto pending = state.behaviors.find(id);
        if (pending != state.behaviors.end() && pending->second.kind == RuntimeBehaviorKind::Move) {
            moveTarget = pending->second.moveTarget;
            moveGroup = pending->second.repackMoveGroup;
        }
        int vehicleId = -1;
        if (!undeployRuntimeStructure(state, id, &vehicleId, selected)) {
            // An external spawn may occupy the exit after folding began.
            // Hold the last folding frame and retry without creating a unit.
            if (auto* source = findMutableObjectById(state, id);
                source != nullptr && runtimeStructureIsRepacking(*source)) {
                source->buildAnimTicksRemaining = 1;
            }
            continue;
        }
        auto* vehicle = findMutableObjectById(state, vehicleId);
        if (vehicle != nullptr && moveTarget) {
            RuntimeObjectBehavior move;
            move.kind = RuntimeBehaviorKind::Move;
            move.state = RuntimeBehaviorState::Queued;
            move.moveTarget = *moveTarget;
            move.repackMoveGroup = std::move(moveGroup);
            state.behaviors[vehicleId] = std::move(move);
            setRuntimeMissionIntent(state, *vehicle, RuntimeMissionIntent::Moving,
                "repackMoveReady");
        }
        // Other pending yards keep this identity until the real conversion;
        // they never soft-ignore its foundation during the folding interval.
        remapRuntimeRepackMoveGroupMember(state, id, vehicleId);
        changed = true;
    }
    return changed;
}

int runtimeImpactAnimationFrameDelayTicks(const ArtType* art, int fixedTickHz) {
    return std::max(1, runtimeRuleFramesToTicks(runtimeRuleAnimationDelayFrames(art ? art->rate : 0), fixedTickHz));
}

int runtimeImpactAnimationFrameForStep(
    const RuntimeImpactEffect& impact,
    int step) {
    if (impact.animationFrameCount <= 0
        || impact.animationSequenceSteps <= 0) {
        return std::max(0, impact.frameIndex);
    }
    const int start = impact.animationStartFrame;
    const int loopStart = impact.animationLoopStartFrame;
    const int loopEnd = impact.animationLoopEndFrame;
    const int end = impact.animationEndFrame;
    const int introSteps = std::max(0, loopStart - start);
    const int loopSpan = std::max(1, loopEnd - loopStart);
    const int loopSteps = loopSpan * std::max(1, impact.animationLoopCount);
    const int clampedStep = std::clamp(
        step,
        0,
        std::max(0, impact.animationSequenceSteps - 1)
    );
    if (clampedStep < introSteps) {
        return start + clampedStep;
    }
    if (clampedStep < introSteps + loopSteps) {
        return loopStart + ((clampedStep - introSteps) % loopSpan);
    }
    return std::clamp(
        loopEnd + (clampedStep - introSteps - loopSteps),
        loopEnd,
        std::max(loopEnd, end - 1)
    );
}

bool runtimeImpactSmudgeMidpointCrossed(
    const RuntimeImpactEffect& impact,
    int elapsedBefore,
    int elapsedAfter,
    int& midpointStep) {
    midpointStep = -1;
    if (impact.animationSequenceSteps <= 0
        || impact.animationLoopEndFrame <= impact.animationLoopStartFrame) {
        return false;
    }
    const int introSteps = std::max(
        0,
        impact.animationLoopStartFrame - impact.animationStartFrame
    );
    const int loopSpan = std::max(
        1,
        impact.animationLoopEndFrame - impact.animationLoopStartFrame
    );
    const int frameDelay = std::max(1, impact.animationFrameDelayTicks);
    for (int loop = 0; loop < std::max(1, impact.animationLoopCount); ++loop) {
        const int candidateStep = introSteps
            + (loop * loopSpan)
            + (loopSpan / 2);
        if (candidateStep <= impact.lastSmudgeMidpointStep) {
            continue;
        }
        const int triggerTick = std::max(1, candidateStep * frameDelay);
        if (elapsedBefore < triggerTick && elapsedAfter >= triggerTick) {
            midpointStep = candidateStep;
            return true;
        }
    }
    return false;
}

void initializeRuntimeImpactAnimationTimeline(
    RuntimeProductionHostState& state,
    RuntimeImpactEffect& impact,
    bool force) {
    const ArtType* art = state.art.findArtType(impact.animId);
    if (art == nullptr || (!force && !art->crater && !art->scorch)) {
        return;
    }

    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = state.artLoaded ? &state.art : nullptr;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(
        state.config,
        state.report.map
    );
    context.structurePalettePath = runtimeStructurePalettePath(
        state.config,
        state.report.map
    );
    context.animationTick = state.tick;
    context.fixedTickHz = state.config.fixedTickHz;

    RuntimeShpFrameImage firstFrame;
    std::string resolvedImageId;
    std::string sourcePath;
    std::string loadError;
    int frameCount = 0;
    const bool mainFramesOnly = art->shadowSpecified && art->shadow;
    if (!loadRuntimeAuxiliaryShpFrameImage(
            context,
            state.renderCache,
            impact.animId,
            0,
            firstFrame,
            resolvedImageId,
            sourcePath,
            loadError,
            mainFramesOnly,
            &frameCount
        )
        || frameCount <= 0) {
        return;
    }

    impact.animationFrameCount = frameCount;
    impact.animationFrameDelayTicks = runtimeImpactAnimationFrameDelayTicks(art, state.config.fixedTickHz);
    impact.animationStartFrame = std::clamp(
        art->hasAnimationStart ? art->animationStart : 0,
        0,
        frameCount - 1
    );
    impact.animationEndFrame = frameCount;
    if (art->hasAnimationEnd) {
        impact.animationEndFrame = std::clamp(
            impact.animationStartFrame + std::max(1, art->animationEnd),
            impact.animationStartFrame + 1,
            frameCount
        );
    }
    impact.animationLoopStartFrame = std::clamp(
        art->hasAnimationLoopStart
            ? art->animationLoopStart
            : impact.animationStartFrame,
        impact.animationStartFrame,
        impact.animationEndFrame - 1
    );
    impact.animationLoopEndFrame = std::clamp(
        art->hasAnimationLoopEnd
            ? art->animationLoopEnd
            : impact.animationEndFrame,
        impact.animationLoopStartFrame + 1,
        impact.animationEndFrame
    );
    impact.animationLoopCount = art->hasAnimationLoopEnd
        ? std::clamp(art->animationLoopCount, 1, 1000)
        : 1;
    const int introSteps = std::max(
        0,
        impact.animationLoopStartFrame - impact.animationStartFrame
    );
    const int loopSpan = std::max(
        1,
        impact.animationLoopEndFrame - impact.animationLoopStartFrame
    );
    const int outroSteps = std::max(
        0,
        impact.animationEndFrame - impact.animationLoopEndFrame
    );
    impact.animationSequenceSteps = introSteps
        + (loopSpan * impact.animationLoopCount)
        + outroSteps;
    impact.frameIndex = impact.animationStartFrame;
    impact.totalTicks = std::max(
        impact.totalTicks,
        impact.animationSequenceSteps * impact.animationFrameDelayTicks
    );
    impact.remainingTicks = impact.totalTicks;

    const int firstMidpointStep = introSteps + (loopSpan / 2);
    const int midpointFrame = runtimeImpactAnimationFrameForStep(
        impact,
        firstMidpointStep
    );
    RuntimeShpFrameImage midpointImage;
    int ignoredFrameCount = 0;
    if (loadRuntimeAuxiliaryShpFrameImage(
            context,
            state.renderCache,
            impact.animId,
            midpointFrame,
            midpointImage,
            resolvedImageId,
            sourcePath,
            loadError,
            mainFramesOnly,
            &ignoredFrameCount
        )) {
        impact.animationMidpointWidth = midpointImage.globalWidth > 0
            ? midpointImage.globalWidth
            : midpointImage.width;
        impact.animationMidpointHeight = midpointImage.globalHeight > 0
            ? midpointImage.globalHeight
            : midpointImage.height;
    }
}

bool runtimeIsVirusDeathAnimation(const RuntimeGameSessionState& state, const RuntimeImpactEffect& impact) {
    return impact.death && !impact.animId.empty()
        && toLowerAsciiLocal(impact.animId) == toLowerAsciiLocal(state.rules.getInfantryVirusAnim());
}

void initializeRuntimeVirusDeath(RuntimeGameSessionState& state, RuntimeImpactEffect& impact) {
    if (!runtimeIsVirusDeathAnimation(state, impact)) return;
    const auto* art = state.art.findArtType(impact.animId);
    impact.lifecycleState = "VirusDeath";
    impact.deferredParticleAnimId = impact.animId;
    const int hz = std::max(1, state.config.fixedTickHz);
    impact.animationFrameDelayTicks = runtimeImpactAnimationFrameDelayTicks(art, hz);
    const int steps = impact.animationSequenceSteps > 0 ? impact.animationSequenceSteps : 35;
    impact.totalTicks = steps * impact.animationFrameDelayTicks;
    impact.remainingTicks = impact.totalTicks;
    impact.deferredParticleTicks = std::max(1, (steps * 3 / 5) * impact.animationFrameDelayTicks);
}

bool advanceRuntimeVirusDeath(RuntimeProductionHostState& state, RuntimeImpactEffect& impact) {
    if (impact.deferredParticleTicks <= 0 || --impact.deferredParticleTicks > 0) return false;
    impact.deferredParticleTicks = -1; // Consumed even when art has no particles.
    RuntimeParticleSpawnContext context;
    context.sourceObjectId = impact.attackerObjectId;
    context.owner = impact.deferredParticleOwner;
    context.sourceTag = "animation:" + impact.deferredParticleAnimId;
    context.position = {
        (impact.hasWorldPosition ? impact.worldX : impact.cell.x) * 256.0,
        (impact.hasWorldPosition ? impact.worldY : impact.cell.y) * 256.0,
        (impact.hasWorldPosition ? impact.worldLevel : impact.level) * 256.0
    };
    context.seed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(impact.projectileActorId)) << 32U)
        ^ static_cast<std::uint32_t>(state.tick) ^ 0xd1e5ULL;
    (void)spawnRuntimeArtAnimationParticles(state, impact.deferredParticleAnimId, context);
    emitRuntimeSound(state, "sound:VirusVictimExplode", runtimeSpatialAudioPositionForImpact(impact), true);
    return true;
}

bool runtimeGrantCrateVeterancy(
    RuntimeProductionHostState& state,
    const RuntimeCrateState& crate,
    const RuntimeCrateCollector& collector,
    const CratePowerupRule& powerup) {
    WorldObjectInstance* collectingObject =
        findMutableObjectById(state, collector.objectId);
    const int cap = std::clamp(
        state.rules.getVeterancyRules().veteranCap,
        0,
        2
    );
    if (collectingObject == nullptr
        || !runtimeVeterancyObjectIsTrainable(state, *collectingObject)
        || collectingObject->veterancy >= cap) {
        return false;
    }
    const int levels = std::max(
        1,
        static_cast<int>(std::lround(powerup.data))
    );
    for (int objectId : runtimeCrateOwnedUnitIdsInRadius(
            state,
            crate,
            collector.owner
        )) {
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr
            || !runtimeVeterancyObjectIsTrainable(state, *object)
            || object->veterancy >= cap) {
            continue;
        }
        const bool hadCloakAbility = runtimeVeterancyHasAbility(
            state,
            *object,
            VeteranAbility::Cloak
        );
        object->veterancy = std::min(cap, object->veterancy + levels);
        object->veterancyExperience = 0;
        if (!hadCloakAbility
            && runtimeVeterancyHasAbility(
                state,
                *object,
                VeteranAbility::Cloak
            )) {
            runtimeDecloakObject(state, *object, "crateVeterancy");
        }
    }
    return true;
}

bool runtimeApplyCrateObjectOnlyDamage(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell,
    int targetObjectId,
    double authoredDamage,
    const std::string& warheadId,
    int salt) {
    if (!std::isfinite(authoredDamage) || authoredDamage <= 0.0) {
        return true;
    }
    RuntimeProjectileEffect impact;
    const long long syntheticActorId = -800000LL
        - std::max(0LL, static_cast<long long>(salt));
    impact.actorId = static_cast<int>(std::clamp(
        syntheticActorId,
        static_cast<long long>(std::numeric_limits<int>::min()),
        -1LL
    ));
    impact.attackerObjectId = -1;
    impact.targetObjectId = targetObjectId;
    impact.owner = "Neutral";
    impact.fromCell = cell;
    impact.toCell = cell;
    impact.currentCell = cell;
    impact.fromLevel = terrainLevelAtRuntimeCell(state, cell.x, cell.y);
    impact.toLevel = impact.fromLevel;
    impact.currentLevel = impact.fromLevel;
    impact.spawnTick = state.tick;
    impact.impactTick = state.tick;
    const double clampedDamage = std::clamp(
        authoredDamage,
        1.0,
        static_cast<double>(std::numeric_limits<int>::max())
    );
    impact.damage = static_cast<int>(std::llround(clampedDamage));
    impact.spawnedMissileDamage = true;
    impact.spawnedMissileBaseDamage = impact.damage;
    impact.veteranDamageMultiplier = 1.0;
    impact.renderIntent = "crateObjectOnly";
    impact.warheadId = trimAsciiLocal(warheadId);
    impact.warheadImpactIntentId = "crateObjectOnly";
    if (const WarheadType* warhead =
            findRuntimeWarheadType(state.rules, impact.warheadId)) {
        const std::vector<std::string> animations =
            splitRuntimeTokenList(warhead->animList);
        if (!animations.empty()) {
            impact.impactAnimId = animations.front();
        } else {
            impact.impactAnimId = trimAsciiLocal(warhead->explosion);
        }
        impact.bright = warhead->bright;
        impact.particleId = trimAsciiLocal(warhead->particle);
    }
    impact.aa = true;
    impact.ag = true;
    impact.affectsAllies = true;
    impact.affectsEnemies = true;
    impact.cellSpread = 0.0;
    return applyRuntimeProjectileImpact(state, impact);
}

bool runtimeOwnerHasSuperWeaponBuilding(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& superWeaponId) {
    const std::string ownerKey =
        runtimeRepairDepotCanonicalOwnerId(state, owner);
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind != WorldObjectKind::Structure
            || runtimeRepairDepotCanonicalOwnerId(state, object.owner)
                != ownerKey) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building != nullptr) {
            for (const auto& id : buildingSuperWeapons(*building))
                if (runtimeCrateTypeIs(id, superWeaponId)) return true;
        }
    }
    return false;
}

void spawnRuntimeImpact(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell,
    int level,
    bool death,
    const std::string& animId,
    const std::string& warheadId,
    int damage,
    double cellSpread,
    bool bright,
    const std::string& particleId,
    const RuntimeProjectileEffect* projectile,
    const RuntimeProjectileTargetPosition* precisePosition,
    const RuntimeAirImpactBodyCenter* bodyCenter,
    bool managedAnimationSound) {
    RuntimeImpactEffect impact;
    if (projectile != nullptr) {
        impact.projectileActorId = projectile->actorId;
        impact.attackerObjectId = projectile->attackerObjectId;
        impact.targetObjectId = projectile->targetObjectId;
        impact.owner = projectile->owner;
        impact.weaponId = projectile->weaponId;
        impact.weaponSlot = projectile->weaponSlot;
        impact.projectileId = projectile->projectileId;
        impact.projectileImageId = projectile->projectileImageId;
        impact.warheadImpactIntentId =
            projectile->warheadImpactIntentId.empty()
                ? projectile->warheadId
                : projectile->warheadImpactIntentId;
        impact.trailerIntentId = projectile->trailerIntentId;
    }
    if (impact.targetObjectId >= 0) {
        impact.targetIsStructure = std::any_of(
            state.world.getObjects().begin(),
            state.world.getObjects().end(),
            [&impact](const WorldObjectInstance& object) {
                return object.instanceId == impact.targetObjectId
                    && object.kind == WorldObjectKind::Structure;
            }
        );
    }
    impact.cell = cell;
    impact.level = level;
    if (precisePosition != nullptr) {
        impact.hasWorldPosition = true;
        impact.worldX = precisePosition->x;
        impact.worldY = precisePosition->y;
        impact.worldLevel = precisePosition->level;
    }
    applyRuntimeAirImpactBodyCenter(impact, bodyCenter);
    impact.death = death;
    impact.totalTicks = runtimeRuleFramesToTicks(death ? 8 : 4, state.config.fixedTickHz);
    impact.remainingTicks = impact.totalTicks;
    impact.radius = death ? 18 : 10;
    impact.lifecycleState = death ? "DeathImpact" : "Impact";
    const bool fallingBomb =
        projectile != nullptr
        && toLowerAsciiLocal(projectile->renderIntent) == "fallingbomb";
    impact.renderIntent = death
        ? "deathImpact"
        : (fallingBomb ? "bombImpact" : "warheadImpact");
    impact.animId = animId;
    impact.warheadId = warheadId;
    if (impact.warheadImpactIntentId.empty()) {
        impact.warheadImpactIntentId = warheadId;
    }
    impact.particleId = particleId;
    impact.damage = damage;
    impact.cellSpread = cellSpread;
    impact.bright = bright;
    if (cellSpread > 0.0) {
        impact.radius = std::max(
            impact.radius,
            static_cast<int>(std::round(cellSpread * 12.0))
        );
        impact.totalTicks = std::max(impact.totalTicks, runtimeRuleFramesToTicks(6, state.config.fixedTickHz));
        impact.remainingTicks = impact.totalTicks;
    }
    if (bright) {
        impact.totalTicks = std::max(impact.totalTicks, runtimeRuleFramesToTicks(7, state.config.fixedTickHz));
        impact.remainingTicks = impact.totalTicks;
    }
    const WarheadType* impactWarhead = findRuntimeWarheadType(state.rules, warheadId);
    const bool mindControlImpact = !death && impactWarhead != nullptr
        && impactWarhead->mindControl;
    if (mindControlImpact) {
        impact.lifecycleState = "MindControlImpact";
    }
    // INITFIRE aliases FIRE01: its first four frames are only tiny sparks.
    // Keep the complete one-shot flame instead of the generic eight-tick hit.
    const bool initiateFlameImpact = !death
        && toUpperAsciiLocal(animId) == "INITFIRE";
    const bool chaosGasImpact = !death && toUpperAsciiLocal(animId) == "CDGAS";
    if (chaosGasImpact && projectile != nullptr && impactWarhead != nullptr && impactWarhead->psychedelic) {
        const auto* weapon = findRuntimeWeaponType(state.rules, projectile->weaponId);
        if (weapon != nullptr && weapon->areaFire) {
            impact.hasWorldPosition = true;
            impact.worldX = projectile->sourceX;
            impact.worldY = projectile->sourceY;
            impact.worldLevel = projectile->sourceZ;
        }
    }
    initializeRuntimeImpactAnimationTimeline(
        state, impact, mindControlImpact || initiateFlameImpact || chaosGasImpact);
    if (!death && projectile != nullptr && runtimeProjectileIsNuclearDeathDetonation(*projectile)) {
        // Use the same complete SHP sequence and retail Rate as a silo
        // nuclear impact; the ordinary short-hit lifetime truncates it.
        initializeRuntimeSuperWeaponAnimation(state, impact);
    }
    if ((initiateFlameImpact || chaosGasImpact) && impact.animationSequenceSteps > 0) {
        impact.totalTicks = impact.animationSequenceSteps * impact.animationFrameDelayTicks;
        impact.remainingTicks = impact.totalTicks;
    }

    if (runtimeIsVirusDeathAnimation(state, impact)) {
        impact.deferredParticleOwner = impact.owner;
        initializeRuntimeVirusDeath(state, impact);
    } else if (!trimAsciiLocal(animId).empty()) {
        RuntimeParticleSpawnContext particleContext;
        particleContext.sourceObjectId = impact.attackerObjectId;
        particleContext.owner = impact.owner;
        particleContext.sourceTag = "animation:" + animId;
        particleContext.position = {
            (impact.hasWorldPosition
                ? impact.worldX
                : static_cast<double>(cell.x)) * 256.0,
            (impact.hasWorldPosition
                ? impact.worldY
                : static_cast<double>(cell.y)) * 256.0,
            (impact.hasWorldPosition
                ? static_cast<double>(impact.worldLevel)
                : static_cast<double>(level)) * 256.0
        };
        particleContext.seed =
            (static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(impact.projectileActorId)
             ) << 32U)
            ^ static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(state.tick)
              )
            ^ (death ? 0xd1e5ULL : 0xa11ULL);
        (void)spawnRuntimeArtAnimationParticles(
            state,
            animId,
            particleContext
        );
    }

    // AnimType StartSound takes precedence over its legacy Report fallback.
    // The separate lethal-hit overlay shares the ordinary impact cue. A
    // private looping placement animation has its own managed sound lifetime.
    if (!death && !managedAnimationSound && !trimAsciiLocal(animId).empty()) {
        const auto* impactArt = state.art.findArtType(animId);
        const auto sound = runtimeArtAnimationSound(state, impactArt);
        if (!sound.empty()) {
            emitRuntimeSound(state, "sound:" + sound, runtimeSpatialAudioPositionForImpact(impact), true);
        }
    }
    state.impactEffects.push_back(impact);
}

bool runtimeImpactCanLeaveSmudge(
    const RuntimeProductionHostState& state,
    const RuntimeImpactEffect& impact,
    const TerrainCell*& terrainOut) {
    terrainOut = findRuntimeTerrainCellAt(state, impact.cell);
    if (terrainOut == nullptr) {
        return false;
    }

    const RuntimeRadarProjection radarProjection =
        buildRuntimeRadarProjection(state);
    if (radarProjection.valid
        && !runtimeRadarTerrainCellIntersectsProjection(
            radarProjection,
            *terrainOut
        )) {
        return false;
    }

    const RuntimeTerrainInfo terrainInfo =
        runtimeCachedTerrainInfoForCell(state, *terrainOut);
    if (terrainInfo.kind != RuntimeTerrainKind::Land
        || terrainInfo.water
        || terrainInfo.shore
        || terrainInfo.cliff
        || terrainInfo.ramp
        || terrainInfo.bridge
        || terrainOut->hasOverlay) {
        return false;
    }
    if (state.theaterLoaded) {
        const TheaterTileReference tile = state.theater.resolveTileIndex(
            terrainOut->tileIndex
        );
        if (!tile.validTileSet || !tile.morphable) {
            return false;
        }
    }

    const double impactLevel = impact.hasWorldPosition
        ? impact.worldLevel
        : static_cast<double>(impact.level);
    constexpr double maximumSmudgeHeightDifference = 30.0 / 256.0;
    return std::abs(
        impactLevel - static_cast<double>(terrainOut->level)
    ) < maximumSmudgeHeightDifference;
}

int runtimeShrapnelCellRing(int dx, int dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

int spawnRuntimeProjectileShrapnel(
    RuntimeProductionHostState& state, const RuntimeProjectileEffect& parent,
    const RuntimeCellPoint& impactCell, int impactLevel) {
    if (parent.generation >= 4) return 0;
    const auto* projectile = findRuntimeProjectileType(state.rules, parent.projectileId);
    if (!projectile || projectile->shrapnelCount <= 0
        || trimAsciiLocal(projectile->shrapnelWeapon).empty()) return 0;
    const auto* weapon = findRuntimeWeaponType(state.rules, projectile->shrapnelWeapon);
    if (!weapon || weapon->neverUse || weapon->damage <= 0 || weapon->range <= 0.0) return 0;

    // The damage path may already have marked the struck unit dead. Its saved
    // identity still describes this hit; do not turn a lethal hit into a miss.
    const auto* struck = findRuntimeObjectByIdIncludingInactive(state, parent.targetObjectId);
    if (!struck) {
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || runtimeObjectIsAirTarget(state, object)) continue;
            const auto cells = object.kind == WorldObjectKind::Structure
                ? runtimeStructureFootprintCellsForObject(state, object)
                : std::vector<RuntimeCellPoint>{runtimeCellPointForObject(object)};
            if (std::any_of(cells.begin(), cells.end(), [&](const auto& cell) {
                return cell.x == impactCell.x && cell.y == impactCell.y;
            })) { struck = &object; break; }
        }
    }
    if (!struck || struck->kind == WorldObjectKind::Structure) return 0;

    struct Target { int objectId; int ring; int dx; int dy; };
    std::vector<Target> targets;
    std::unordered_set<int> occupied;
    const int range = static_cast<int>(std::clamp(std::floor(weapon->range), 0.0, 256.0));
    for (const auto& candidate : state.world.getObjects()) {
        if (!candidate.alive || runtimeObjectIsAirTarget(state, candidate)) continue;
        const auto cells = candidate.kind == WorldObjectKind::Structure
            ? runtimeStructureFootprintCellsForObject(state, candidate)
            : std::vector<RuntimeCellPoint>{runtimeCellPointForObject(candidate)};
        for (const auto& cell : cells) {
            const int dx = cell.x - impactCell.x, dy = cell.y - impactCell.y;
            const int ring = runtimeShrapnelCellRing(dx, dy);
            if (ring == 0 || ring > range || !occupied.insert(runtimeCellKey(cell.x, cell.y)).second) continue;
            // Retail checks the first ground object in each cell, and only
            // excludes the firer and allies. A building can occupy many cells.
            if (candidate.instanceId == parent.attackerObjectId
                || runtimeOwnersAreAllied(state, candidate.owner, parent.owner)) continue;
            targets.push_back({candidate.instanceId, ring, dx, dy});
        }
    }
    std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) {
        if (a.ring != b.ring) return a.ring < b.ring;
        if (a.dy != b.dy) return a.dy < b.dy;
        // The first three entries of the retail table run right to left.
        return a.ring == 1 && a.dy == -1 ? a.dx > b.dx : a.dx < b.dx;
    });

    WorldObjectInstance source;
    source.instanceId = parent.attackerObjectId; source.kind = WorldObjectKind::Unit;
    source.owner = parent.owner; source.type = "SHRAPNEL";
    source.x = impactCell.x; source.y = impactCell.y;
    source.cell = runtimeCellKey(source.x, source.y); source.level = impactLevel;
    source.facing = source.turretFacing = parent.targetFacing; source.alive = true;
    RuntimeWeaponProfile profile;
    finalizeRuntimeWeaponProfile(profile, state, RuntimeWeaponSlot::Indexed, *weapon, "shrapnel");
    auto random = runtimeCombatRandomMix(state.config.simulationRandomSeed
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(parent.actorId)) << 32U)
        ^ static_cast<std::uint32_t>(parent.attackerObjectId) ^ 0x53485241504e454cULL);
    const int count = std::min(projectile->shrapnelCount, 256);
    for (int i = 0; i < count; ++i) {
        WorldObjectInstance ground;
        const WorldObjectInstance* target = i < static_cast<int>(targets.size())
            ? findObjectById(state, targets[i].objectId) : nullptr;
        if (!target) {
            // gamemd 0x46A958/0x46A976: independently draw X and Y from [-2,2].
            random = runtimeCombatRandomMix(random); ground.x = impactCell.x + static_cast<int>(random % 5) - 2;
            random = runtimeCombatRandomMix(random); ground.y = impactCell.y + static_cast<int>(random % 5) - 2;
            ground.instanceId = -1; ground.kind = WorldObjectKind::Unit;
            ground.owner = "Neutral"; ground.type = "SHRAPNELCELL"; ground.alive = true;
            ground.cell = runtimeCellKey(ground.x, ground.y);
            ground.level = terrainLevelAtRuntimeCell(state, ground.x, ground.y);
            target = &ground;
        }
        const int damage = runtimeAuthoredDamageAgainst(state, *target,
            static_cast<long long>(weapon->damage) * std::max(1, weapon->burst),
            weapon->warhead, 0.0, parent.veteranDamageMultiplier);
        spawnRuntimeProjectile(state, source, *target, *weapon, profile, damage,
            parent.actorId, parent.generation + 1, true, parent.targetObjectId, parent.veteranDamageMultiplier,
            parent.deathWeaponCascadeId, parent.deathWeaponRequestId, parent.deathWeaponCascadeDepth,
            true, &parent);
    }
    return count;
}

int spawnRuntimeProjectileAirburst(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& parent,
    const RuntimeCellPoint& impactCell,
    int impactLevel) {
    if (parent.generation >= 4) {
        return 0;
    }
    const ProjectileType* parentProjectile =
        findRuntimeProjectileType(state.rules, parent.projectileId);
    if (parentProjectile == nullptr
        || !parentProjectile->airburst
        || parentProjectile->cluster <= 0
        || trimAsciiLocal(parentProjectile->airburstWeapon).empty()) {
        return 0;
    }
    const WeaponType* childWeapon = findRuntimeWeaponType(
        state.rules,
        parentProjectile->airburstWeapon
    );
    if (childWeapon == nullptr
        || childWeapon->neverUse
        || childWeapon->damage == 0
        || childWeapon->range <= 0.0) {
        return 0;
    }

    WorldObjectInstance source;
    source.instanceId = parent.attackerObjectId;
    source.kind = WorldObjectKind::Unit;
    source.owner = parent.owner;
    source.type = "AIRBURST";
    source.x = impactCell.x;
    source.y = impactCell.y;
    source.cell = runtimeCellKey(impactCell.x, impactCell.y);
    source.level = impactLevel;
    source.facing = parent.targetFacing;
    source.turretFacing = parent.targetFacing;
    source.alive = true;

    RuntimeWeaponProfile childProfile;
    finalizeRuntimeWeaponProfile(
        childProfile,
        state,
        RuntimeWeaponSlot::Indexed,
        *childWeapon,
        "airburst"
    );
    const long long authoredDamage = static_cast<long long>(childWeapon->damage)
        * std::max(1, childWeapon->burst);
    const double scaledDamage = static_cast<double>(authoredDamage)
        * parent.veteranDamageMultiplier;
    const int damage = static_cast<int>(std::lround(std::clamp(scaledDamage,
        static_cast<double>(std::numeric_limits<int>::min()),
        static_cast<double>(std::numeric_limits<int>::max()))));
    const int spawnCount = std::clamp(parentProjectile->cluster, 1, 64);
    std::vector<RuntimeCellPoint> offsets;
    offsets.reserve(static_cast<std::size_t>(spawnCount));
    offsets.push_back({0, 0});
    for (int radius = 1;
        static_cast<int>(offsets.size()) < spawnCount;
        ++radius) {
        for (int dy = -radius;
            dy <= radius && static_cast<int>(offsets.size()) < spawnCount;
            ++dy) {
            for (int dx = -radius;
                dx <= radius && static_cast<int>(offsets.size()) < spawnCount;
                ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) == radius) {
                    offsets.push_back({dx, dy});
                }
            }
        }
    }

    for (const RuntimeCellPoint& offset : offsets) {
        WorldObjectInstance target;
        target.instanceId = -1;
        target.kind = WorldObjectKind::Unit;
        target.owner = "Neutral";
        target.type = "AIRBURSTCELL";
        target.x = impactCell.x + offset.x;
        target.y = impactCell.y + offset.y;
        target.cell = runtimeCellKey(target.x, target.y);
        target.level = terrainLevelAtRuntimeCell(
            state,
            target.x,
            target.y
        );
        target.alive = true;
        spawnRuntimeProjectile(
            state,
            source,
            target,
            *childWeapon,
            childProfile,
            damage,
            parent.actorId,
            parent.generation + 1,
            false,
            -1,
            parent.veteranDamageMultiplier,
            parent.deathWeaponCascadeId,
            parent.deathWeaponRequestId,
            parent.deathWeaponCascadeDepth,
            false,
            &parent
        );
    }
    return spawnCount;
}

int spawnRuntimeSpawnedMissileTerminalWeapon(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& parent,
    const RuntimeCellPoint& impactCell,
    int impactLevel) {
    if (!parent.spawnedMissileDamage || parent.generation >= 4) {
        return 0;
    }
    const SpawnedMissileProfile* missile =
        state.rules.findSpawnedMissileProfile(parent.projectileImageId);
    if (missile == nullptr
        || trimAsciiLocal(missile->terminalWeapon).empty()) {
        return 0;
    }
    const WeaponType* terminalWeapon = findRuntimeWeaponType(
        state.rules,
        missile->terminalWeapon
    );
    if (terminalWeapon == nullptr || terminalWeapon->neverUse) {
        return 0;
    }

    // Spawned missiles (notably stock V3ROCKET) invoke an unlisted virtual
    // weapon at their terminal coordinate. Build cell-local causal proxies so
    // Dropping resolves at the impact point instead of back at the launcher.
    WorldObjectInstance source;
    source.instanceId = parent.attackerObjectId;
    source.kind = WorldObjectKind::Unit;
    source.owner = parent.owner;
    source.type = parent.launchSourceType.empty()
        ? "SPAWNEDMISSILE"
        : parent.launchSourceType;
    source.x = impactCell.x;
    source.y = impactCell.y;
    source.cell = runtimeCellKey(impactCell.x, impactCell.y);
    source.level = impactLevel;
    source.facing = parent.targetFacing;
    source.turretFacing = parent.targetFacing;
    source.alive = true;

    WorldObjectInstance target;
    target.instanceId = -1;
    target.kind = WorldObjectKind::Unit;
    target.owner = "Neutral";
    target.type = "SPAWNEDMISSILECELL";
    target.x = impactCell.x;
    target.y = impactCell.y;
    target.cell = runtimeCellKey(impactCell.x, impactCell.y);
    target.level = impactLevel;
    target.alive = true;

    RuntimeWeaponProfile terminalProfile;
    finalizeRuntimeWeaponProfile(
        terminalProfile,
        state,
        RuntimeWeaponSlot::Indexed,
        *terminalWeapon,
        "spawnedMissileTerminal"
    );
    const long long authoredDamage =
        static_cast<long long>(terminalWeapon->damage)
        * static_cast<long long>(std::max(1, terminalWeapon->burst));
    const double scaledDamage = static_cast<double>(authoredDamage)
        * std::max(0.0, parent.veteranDamageMultiplier);
    const int damage = !std::isfinite(scaledDamage)
            || scaledDamage >= static_cast<double>(
                std::numeric_limits<int>::max()
            )
        ? std::numeric_limits<int>::max()
        : (scaledDamage <= static_cast<double>(
                std::numeric_limits<int>::min()
            )
            ? std::numeric_limits<int>::min()
            : static_cast<int>(std::lround(scaledDamage)));
    spawnRuntimeProjectile(
        state,
        source,
        target,
        *terminalWeapon,
        terminalProfile,
        damage,
        parent.actorId,
        parent.generation + 1,
        false,
        -1,
        parent.veteranDamageMultiplier,
        parent.deathWeaponCascadeId,
        parent.deathWeaponRequestId,
        parent.deathWeaponCascadeDepth,
        false,
        &parent
    );
    return 1;
}

std::optional<WorldObjectInstance> runtimeGroundAttackTargetObject(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell) {
    if (cell.x < 0 || cell.x > runtimeMultiplayerGameplayMaximumCellCoordinate
        || cell.y < 0 || cell.y > runtimeMultiplayerGameplayMaximumCellCoordinate) {
        return std::nullopt;
    }
    const auto terrain = state.terrainByCell.find(runtimeCellKey(cell.x, cell.y));
    if (terrain == state.terrainByCell.end() || terrain->second == nullptr) {
        return std::nullopt;
    }
    WorldObjectInstance target;
    target.instanceId = -1960000 - runtimeCellKey(cell.x, cell.y);
    target.kind = WorldObjectKind::Structure;
    target.type = "RuntimeGround";
    target.owner = "RuntimeGround";
    target.health = 1;
    target.x = cell.x;
    target.y = cell.y;
    target.cell = runtimeCellKey(cell.x, cell.y);
    target.level = terrainLevelAtRuntimeCell(state, cell.x, cell.y);
    target.alive = true;
    return target;
}

bool beginRuntimeBuildingCollapse(
    RuntimeProductionHostState& state, const WorldObjectInstance& object, const BuildingType& building) {
    if (runtimeBuildingCollapseForObject(&state.buildingCollapses, object.instanceId,
            state.tick, state.config.fixedTickHz)) return true;
    if (!object.alive || object.kind != WorldObjectKind::Structure) return false;
    auto animations = splitRuntimeTokenList(building.destroyAnim);
    animations.erase(std::remove_if(animations.begin(), animations.end(), [&](const auto& id) {
        return state.art.findArtType(id) == nullptr;
    }), animations.end());
    if (animations.empty()) return false;
    std::uint64_t seed = runtimeStableHashString("buildingDestroyAnim");
    runtimeStableHashMix(seed, object.instanceId); runtimeStableHashMix(seed, state.tick);
    const auto& anim = animations[seed % animations.size()];
    const auto* art = state.art.findArtType(anim);
    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = &state.art; context.rules = &state.rules;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    RuntimeShpFrameImage image;
    std::string resolved, source, error;
    int frames = 0;
    if (!loadRuntimeAuxiliaryShpFrameImage(context, state.renderCache, anim, 0,
            image, resolved, source, error, art->shadow, &frames) || frames <= 0) return false;
    state.buildingCollapses.push_back({object.instanceId, anim, state.tick, frames,
        art->rate > 0 ? art->rate : 150});
    if (!art->reportSound.empty()) emitRuntimeSound(state, "sound:" + art->reportSound, runtimeSpatialAudioPositionForObject(object), true);
    return true;
}

bool advanceRuntimeBuildingCollapses(RuntimeGameSessionState& state) {
    const bool changed = !state.buildingCollapses.empty();
    std::erase_if(state.buildingCollapses, [&](const auto& collapse) {
        return runtimeBuildingCollapseFrame(collapse, state.tick, state.config.fixedTickHz) >= collapse.frameCount;
    });
    return changed;
}

void setRuntimeInfantryDeathTimeline(
    RuntimeImpactEffect& effect, int start, int count, int delay, int holdTicks) {
    effect.animationFrameCount = start + count;
    effect.animationFrameDelayTicks = delay;
    effect.animationStartFrame = start;
    effect.animationLoopStartFrame = start;
    effect.animationLoopEndFrame = start + count;
    effect.animationEndFrame = start + count;
    effect.animationLoopCount = 1;
    effect.animationSequenceSteps = count;
    effect.frameIndex = start;
    effect.totalTicks = count * delay + holdTicks;
    effect.remainingTicks = effect.totalTicks;
}

bool spawnRuntimeInfantryDeathSequence(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const UnitType& unit,
    const RuntimeProjectileEffect& projectile) {
    const WarheadType* warhead = findRuntimeWarheadType(state.rules, projectile.warheadId);
    if (warhead != nullptr && warhead->temporal) {
        return false;
    }
    const int infDeath = warhead == nullptr ? 0 : warhead->infDeath;
    const std::string& special = state.rules.getInfantryDeathAnim(infDeath);
    if (!unit.notHuman && !special.empty() && state.art.findArtType(special) != nullptr) {
        return false;
    }
    const bool inWater = !object.onBridge && !object.airborne
        && runtimeProductionCellIsOpenSurfaceWater(state, {object.x, object.y});
    const auto sequence = runtimeInfantryDeathSequence(
        state.art, unit, infDeath, object.facing, inWater,
        runtimeArcticTheater(state.theaterLoaded ? state.theater.getTileExtension() : state.report.map.theater)
    );
    if (!sequence.has_value()) {
        return false;
    }
    RuntimeImpactEffect effect;
    effect.projectileActorId = projectile.actorId;
    effect.attackerObjectId = projectile.attackerObjectId;
    effect.targetObjectId = object.instanceId;
    effect.owner = object.owner;
    effect.cell = {object.x, object.y};
    effect.level = object.level;
    effect.hasWorldPosition = true;
    const WorldObjectCellOffset offset = worldObjectSubCellOffset(object);
    effect.worldX = runtimeObjectRenderX(object) + offset.x;
    effect.worldY = runtimeObjectRenderY(object) + offset.y;
    effect.worldLevel = runtimeDrawLevelForObject(object);
    effect.facing = object.facing;
    effect.death = true;
    effect.lifecycleState = "InfantryDeath";
    effect.renderIntent = "infantryDeath";
    effect.animId = sequence->artId;
    effect.warheadId = projectile.warheadId;
    const int tickHz = std::clamp(state.config.fixedTickHz, 1, 240);
    setRuntimeInfantryDeathTimeline(effect, sequence->startFrame, sequence->frameCount,
        std::max(1, tickHz / 15), tickHz);
    state.impactEffects.push_back(std::move(effect));
    return true;
}

void syncRuntimeVoxelDebrisPosition(RuntimeProjectileEffect& debris) {
    const auto& p = debris.voxelDebris.position;
    debris.currentX = p[0] / 256.0; debris.currentY = p[1] / 256.0;
    debris.currentZ = p[2] * runtimeDebrisHeightPerLepton;
    debris.currentCell = {static_cast<int>(std::lround(debris.currentX)), static_cast<int>(std::lround(debris.currentY))};
    debris.currentLevel = static_cast<int>(std::lround(debris.currentZ));
}

int spawnRuntimeUnitDebris(RuntimeProductionHostState& state, const WorldObjectInstance& destroyed,
    const UnitType& unit, const RuntimeProjectileEffect& cause) {
    if (unit.maxDebris <= 0) return 0;
    auto voxelIds = splitRuntimeTokenList(unit.debrisTypes);
    voxelIds.erase(std::remove_if(voxelIds.begin(), voxelIds.end(), [](const auto& id) {
        const auto key = toLowerAsciiLocal(id); return key == "none" || key == "<none>";
    }), voxelIds.end());
    if (voxelIds.empty()) {
        // Shared SHP list/count path, including repeated entries and the global
        // MetallicDebris fallback. Keep the accepted building implementation.
        BuildingType definition; definition.minDebris = unit.minDebris; definition.maxDebris = unit.maxDebris;
        definition.debrisAnims = unit.debrisAnims;
        return spawnRuntimeBuildingDebris(state, destroyed, definition, &cause, true);
    }
    std::uint64_t seed = runtimeStableHashString("unitVoxelDebris");
    runtimeStableHashMixString(seed, destroyed.type); runtimeStableHashMix(seed, destroyed.instanceId);
    runtimeStableHashMix(seed, state.tick);
    const int lower = std::min(unit.minDebris, unit.maxDebris - 1);
    const int upper = std::max(unit.minDebris, unit.maxDebris - 1);
    int budget = lower + static_cast<int>(runtimeDebrisRandom(seed) % (static_cast<std::uint64_t>(upper - lower) + 1));
    if (budget <= 0) return 0;
    const auto authoredMaximums = splitRuntimeTokenList(unit.debrisMaximums);
    std::vector<int> maximums(voxelIds.size(), 0);
    bool canEmit = false;
    for (std::size_t i = 0; i < voxelIds.size(); ++i) {
        if (i < authoredMaximums.size()) (void)parseRuntimeInt(authoredMaximums[i], maximums[i]);
        maximums[i] = std::max(0, maximums[i]);
        canEmit = canEmit || (maximums[i] > 0 && state.rules.findVoxelAnimType(voxelIds[i]));
    }
    // Missing types/maxima and all-zero lists would otherwise stall retail's
    // retry loop. Invalid mod data produces no substitute animation.
    if (!canEmit) return 0;
    const int hz = std::max(1, state.config.fixedTickHz);
    const auto origin = runtimeProjectileTargetPosition(state, destroyed);
    int spawned = 0;
    const std::uint64_t attemptLimit = (static_cast<std::uint64_t>(budget) + 1) * voxelIds.size() * 128;
    for (std::uint64_t attempt = 0; budget > 0 && attempt < attemptLimit; ++attempt) {
        const auto index = static_cast<std::size_t>(attempt % voxelIds.size());
        const auto* type = state.rules.findVoxelAnimType(voxelIds[index]);
        if (!type || maximums[index] <= 0) continue;
        const int count = std::min(budget, static_cast<int>(runtimeDebrisRandom(seed)
            % (static_cast<std::uint64_t>(maximums[index]) + 1)));
        for (int i = 0; i < count; ++i) {
            RuntimeProjectileEffect debris;
            debris.actorId = nextRuntimeProjectileActorId(state);
            debris.attackerObjectId = destroyed.instanceId; debris.sourceAnchorObjectId = destroyed.instanceId;
            debris.owner = destroyed.owner; debris.launchSourceKind = static_cast<int>(destroyed.kind);
            debris.launchSourceType = destroyed.type; debris.launchSourceAuthoredAircraft = destroyed.authoredAircraft;
            debris.launchSourceValidated = destroyed.instanceId >= 0 && !destroyed.owner.empty() && !destroyed.type.empty();
            debris.voxelDebris = runtimeMakeVoxelDebris(origin.x, origin.y, runtimeDrawLevelForObject(destroyed),
                type->duration, type->elasticity, type->minZVelocity, type->maxZVelocity, type->maxXYVelocity,
                type->minAngularVelocity, type->maxAngularVelocity, seed);
            syncRuntimeVoxelDebrisPosition(debris);
            debris.hasSourcePosition = debris.hasTargetPosition = true;
            debris.sourceX = debris.targetX = debris.currentX; debris.sourceY = debris.targetY = debris.currentY;
            debris.sourceZ = debris.targetZ = debris.currentZ;
            debris.fromCell = debris.toCell = debris.currentCell;
            debris.fromLevel = debris.toLevel = debris.currentLevel;
            debris.spawnTick = state.tick;
            debris.totalTicks = debris.remainingTicks = std::max(1, static_cast<int>((static_cast<long long>(type->duration) * hz + 14) / 15));
            debris.impactTick = state.tick + debris.totalTicks;
            debris.renderIntent = "unitVoxelDebris"; debris.weaponId = "UnitDebris"; debris.weaponSlot = "debris";
            debris.projectileId = type->id; debris.projectileImageId = type->image.empty() ? type->id : type->image;
            debris.impactAnimId = type->expireAnim; debris.trailerAnimId = type->trailerAnim;
            debris.damage = type->damage; debris.warheadId = type->warhead;
            debris.cellSpread = type->damageRadius / 256.0;
            debris.affectsAllies = debris.affectsEnemies = true;
            recordRuntimeProjectileReplayEvent(state, debris, "ProjectileSpawned");
            state.projectiles.push_back(std::move(debris)); ++spawned;
        }
        budget -= count;
    }
    state.actionSummary.projectilesInFlight = static_cast<int>(state.projectiles.size());
    return spawned;
}

RuntimeDebrisSurface runtimeVoxelDebrisSurface(const RuntimeGameSessionState& state,
    const std::array<float,3>& previous, const std::array<float,3>& next) {
    const int x = static_cast<int>(std::lround(next[0] / 256.0));
    const int y = static_cast<int>(std::lround(next[1] / 256.0));
    const auto found = state.terrainByCell.find(runtimeCellKey(x,y));
    RuntimeDebrisSurface surface;
    if (found == state.terrainByCell.end() || !found->second) { surface.exists = false; return surface; }
    const auto& terrain = *found->second;
    surface.height = terrain.level / runtimeDebrisHeightPerLepton;
    const auto info = runtimeCachedTerrainInfoForCell(state, terrain);
    surface.water = info.water && !info.bridge;
    if (runtimeOverlayProvidesBridgeDeck(terrain)) {
        const double deck = runtimeEffectiveTerrainLevel(terrain) / runtimeDebrisHeightPerLepton;
        if (previous[2] >= deck) { surface.height = deck; surface.water = false; }
        else if (next[2] >= deck && runtimeOverlayLooksHighBridge(terrain)) {
            surface.height = deck - 20.0; surface.ceiling = true; surface.water = false;
        }
    }
    return surface;
}

void spawnRuntimeVoxelDebrisAnimation(RuntimeProductionHostState& state, const RuntimeProjectileEffect& debris,
    const std::string& id) {
    if (id.empty() || !state.art.findArtType(id)) return;
    RuntimeProjectileTargetPosition position;
    position.cell = debris.currentCell; position.x = debris.currentX; position.y = debris.currentY;
    position.level = debris.currentLevel;
    spawnRuntimeImpact(state, position.cell, position.level, false, id, "", 0, 0.0, false, "", &debris, &position);
    if (!state.impactEffects.empty()) state.impactEffects.back().worldLevel = debris.currentZ;
}

bool advanceRuntimeVoxelDebris(RuntimeProductionHostState& state, RuntimeProjectileEffect& debris) {
    auto& motion = debris.voxelDebris;
    const auto* type = state.rules.findVoxelAnimType(debris.projectileId);
    if (!type) { debris.remainingTicks = 0; return false; }
    const int hz = std::max(1, state.config.fixedTickHz);
    ++debris.elapsedTicks; debris.remainingTicks = std::max(0, debris.totalTicks - debris.elapsedTicks);
    debris.progress = std::clamp(static_cast<double>(debris.elapsedTicks) / debris.totalTicks, 0.0, 1.0);
    debris.lifecycle = RuntimeProjectileLifecycleState::Traveling;
    debris.lifecycleState = runtimeProjectileLifecycleName(debris.lifecycle);
    motion.frameRemainder += 15;
    while (motion.frameRemainder >= hz) {
        motion.frameRemainder -= hz;
        // Retail decrements Duration first; zero duration expires before motion.
        if (motion.framesLeft > 0) --motion.framesLeft;
        if (motion.framesLeft <= 0) {
            const auto surface = runtimeVoxelDebrisSurface(state, motion.position, motion.position);
            if (surface.water && motion.position[2] <= surface.height + 10.0) {
                spawnRuntimeVoxelDebrisAnimation(state, debris, state.rules.getWake());
                const auto splashes = splitRuntimeTokenList(state.rules.getAudioVisualValue("SplashList"));
                if (!splashes.empty()) spawnRuntimeVoxelDebrisAnimation(state, debris,
                    splashes[static_cast<std::size_t>(debris.actorId) % splashes.size()]);
            } else spawnRuntimeVoxelDebrisAnimation(state, debris, type->expireAnim);
            debris.remainingTicks = 0; return false;
        }
        ++motion.frameCount;
        const auto contact = runtimeStepVoxelDebris(motion, [&](const auto& previous, const auto& next) {
            return runtimeVoxelDebrisSurface(state, previous, next);
        });
        syncRuntimeVoxelDebrisPosition(debris);
        if (contact == RuntimeDebrisContact::Outside) { debris.remainingTicks = 0; return false; }
        if (contact == RuntimeDebrisContact::Bounce) {
            spawnRuntimeVoxelDebrisAnimation(state, debris, type->bounceAnim);
            if (debris.damage > 0) {
                auto hit = debris; hit.voxelDebris = {}; hit.renderIntent = "unitDebrisImpact";
                hit.toCell = hit.currentCell; hit.targetX = hit.currentX; hit.targetY = hit.currentY;
                hit.targetZ = hit.currentZ; hit.toLevel = hit.currentLevel; hit.impactAnimId.clear();
                (void)applyRuntimeProjectileImpact(state, hit);
            }
        }
        if ((motion.frameCount % 2) == 0) spawnRuntimeVoxelDebrisAnimation(state, debris, type->trailerAnim);
        if (contact == RuntimeDebrisContact::Stopped || contact == RuntimeDebrisContact::Water) motion.framesLeft = 0;
    }
    return true;
}

bool beginRuntimeWreck(RuntimeProductionHostState& state, const WorldObjectInstance& destroyed,
    const UnitType& unit, const RuntimeProjectileEffect& cause, bool wasAliveBeforeDamage) {
    if (destroyed.kind == WorldObjectKind::Structure || (!destroyed.alive && !wasAliveBeforeDamage)
        || destroyed.garrisonHostObjectId >= 0 || destroyed.mission == "Passenger") return false;
    for (const auto& wreck : state.wrecks)
        if (wreck.object.instanceId == destroyed.instanceId) return true;
    const auto terrain = state.terrainByCell.find(runtimeCellKey(destroyed.x, destroyed.y));
    const TerrainCell* ground = terrain == state.terrainByCell.end() ? nullptr : terrain->second;
    const auto terrainInfo = ground ? runtimeTerrainInfoForCell(state, *ground) : RuntimeTerrainInfo{};
    const bool water = ground && terrainInfo.water && !terrainInfo.bridge && !destroyed.onBridge;
    const bool drownedVehicle = water && !destroyed.onBridge
        && runtimeVehicleCannotSurviveOpenWaterAtCell(state, destroyed, {destroyed.x, destroyed.y});
    const bool sinking = drownedVehicle || (water && unit.naval && !unit.underwater && !unit.organic
        && unit.weight >= unit.shipSinkingWeight && !destroyed.airborne);
    const bool aircraftType = state.rules.findUnitType("AircraftType", destroyed.type) != nullptr;
    const bool crashing = destroyed.airborne && !unit.missileSpawn && (aircraftType || unit.crashable);
    if (!sinking && !crashing) return false;
    RuntimeWreckState wreck;
    wreck.object = destroyed;
    wreck.object.transportPassengerIds.clear(); wreck.object.garrisonOccupantIds.clear();
    wreck.object.alive = false; wreck.object.health = 0;
    wreck.startTick = state.tick; wreck.sinking = sinking;
    wreck.jumpjet = unit.jumpJet || toLowerAsciiLocal(unit.locomotor).find("92612c46") != std::string::npos;
    wreck.tilt = !wreck.jumpjet || unit.tiltCrashJumpjet;
    wreck.x = runtimeObjectRenderX(destroyed); wreck.y = runtimeObjectRenderY(destroyed);
    wreck.groundLevel = ground ? runtimeEffectiveTerrainLevel(*ground) : 0.0;
    // Include the existing aircraft draw-origin lift so the first retained
    // frame joins the live sprite exactly, then let it reach the surface.
    wreck.startLevel = runtimeDrawLevelForObject(destroyed) + (crashing ? 34.0 / 15.0 : 0.0);
    wreck.fallSpeed = std::max(1.0, unit.jumpjetCrash);
    wreck.gravity = std::max(0.01, unit.crashGravity);
    if (crashing && !wreck.jumpjet) {
        const double heading = destroyed.facing * 6.283185307179586 / 256.0;
        const double speed = std::clamp(unit.speed / 256.0, 0.0, 0.25);
        wreck.driftX = (std::sin(heading) - std::cos(heading)) * speed / std::sqrt(2.0);
        wreck.driftY = (-std::sin(heading) - std::cos(heading)) * speed / std::sqrt(2.0);
    }
    wreck.impactAnim = runtimeUnitDestructionAnim(state, destroyed, unit, cause);
    wreck.warhead = cause.warheadId;
    state.wrecks.push_back(std::move(wreck));
    if (drownedVehicle) {
        const auto& retained = state.wrecks.back();
        spawnRuntimeWreckAnimation(state, retained,
            runtimeWreckObjectPose(retained, state.tick, state.config.fixedTickHz),
            "H2O_EXP3", true, "WreckWaterEntry");
    }
    const std::string sound = sinking ? unit.sinkingSound : unit.crashingSound;
    if (!trimAsciiLocal(sound).empty()) emitRuntimeSound(state, "sound:" + trimAsciiLocal(sound), runtimeSpatialAudioPositionForObject(destroyed), true);
    if (crashing) {
        const auto voices = splitRuntimeTokenList(unit.voiceCrashing);
        if (!voices.empty()) emitRuntimeSound(state, "sound:" + voices[static_cast<std::size_t>(destroyed.instanceId) % voices.size()], runtimeSpatialAudioPositionForObject(destroyed), true);
    }
    return true;
}

void spawnRuntimeWreckAnimation(RuntimeProductionHostState& state, const RuntimeWreckState& wreck,
    const WorldObjectInstance& pose, const std::string& animation, bool water, const char* phase) {
    if (animation.empty() || !state.art.findArtType(animation)) return;
    RuntimeProjectileTargetPosition position;
    position.x = pose.renderX; position.y = pose.renderY;
    position.cell = {pose.x, pose.y};
    position.level = static_cast<int>(std::lround(water ? wreck.groundLevel : pose.renderLevel));
    spawnRuntimeImpact(state, position.cell, position.level, !water, animation, wreck.warhead,
        0, 0.0, true, "", nullptr, &position);
    if (!state.impactEffects.empty()) {
        auto& effect = state.impactEffects.back();
        effect.owner = wreck.object.owner;
        effect.worldX = position.x; effect.worldY = position.y;
        effect.worldLevel = water ? wreck.groundLevel : pose.renderLevel;
        effect.renderIntent = water ? "waterWake" : "unitDestruction";
        effect.lifecycleState = phase;
        if (water) {
            // Retain the chosen authored water animation in the canonical
            // impact record and advance its complete native frame sequence.
            effect.warheadImpactIntentId = animation;
            initializeRuntimeImpactAnimationTimeline(state, effect, true);
        }
    }
}

bool advanceRuntimeWrecks(RuntimeProductionHostState& state) {
    if (state.wrecks.empty()) return false;
    std::size_t kept = 0;
    for (auto& wreck : state.wrecks) {
        const double frames = runtimeWreckAgeFrames(wreck, state.tick, state.config.fixedTickHz);
        auto pose = runtimeWreckObjectPose(wreck, state.tick, state.config.fixedTickHz);
        const auto terrain = state.terrainByCell.find(runtimeCellKey(pose.x, pose.y));
        const TerrainCell* ground = terrain == state.terrainByCell.end() ? nullptr : terrain->second;
        const auto info = ground ? runtimeTerrainInfoForCell(state, *ground) : RuntimeTerrainInfo{};
        const double groundLevel = ground ? runtimeEffectiveTerrainLevel(*ground) : wreck.groundLevel;
        const bool water = ground && info.water && !info.bridge;
        const bool finished = wreck.sinking ? runtimeWreckFallLeptons(wreck, frames) > 400.0
            : pose.renderLevel <= groundLevel || !ground;
        const auto* unit = findRuntimeUnitTypeById(state.rules, wreck.object.type);
        const int previousFrame = static_cast<int>(runtimeWreckAgeFrames(wreck, state.tick - 1, state.config.fixedTickHz));
        const int currentFrame = static_cast<int>(frames);
        if (!finished && currentFrame / 4 > previousFrame / 4) {
            if (wreck.sinking) {
                std::uint64_t seed = runtimeStableHashString("sinkingWake");
                runtimeStableHashMix(seed, wreck.object.instanceId); runtimeStableHashMix(seed, currentFrame / 4);
                pose.renderX += (static_cast<int>(seed % 341) - 170) / 256.0;
                pose.renderY += (static_cast<int>((seed >> 16) % 341) - 170) / 256.0;
                spawnRuntimeWreckAnimation(state, wreck, pose, state.rules.getWake(), true, "WreckWake");
            } else {
                spawnRuntimeWreckAnimation(state, wreck, pose, "SMOKEY", false, "WreckSmoke");
            }
        }
        if (finished) {
            if (!wreck.sinking && ground) {
                pose.renderLevel = groundLevel;
                pose.level = static_cast<int>(groundLevel); pose.airborne = false;
                pose.onBridge = info.bridge; pose.renderOnBridge = info.bridge;
                RuntimeProjectileEffect cause; cause.warheadId = wreck.warhead;
                const bool infantrySequence = !water && unit && pose.kind == WorldObjectKind::Infantry
                    && spawnRuntimeInfantryDeathSequence(state, pose, *unit, cause);
                if (!infantrySequence) {
                    // The landing surface can differ from the cell under the
                    // aircraft at destruction (slopes, bridges and shorelines).
                    auto landing = wreck; landing.groundLevel = groundLevel;
                    spawnRuntimeWreckAnimation(state, landing, pose, water ? "H2O_EXP3" : wreck.impactAnim,
                        water, water ? "WreckWaterImpact" : "WreckImpact");
                }
                const std::string sound = unit ? (water ? unit->impactWaterSound : unit->impactLandSound) : std::string();
                if (!sound.empty()) emitRuntimeSound(state, "sound:" + sound, runtimeSpatialAudioPositionForObject(pose));
            }
            continue;
        }
        if (&state.wrecks[kept] != &wreck) state.wrecks[kept] = std::move(wreck);
        ++kept;
    }
    state.wrecks.resize(kept);
    return true;
}

bool runtimeProjectileHasDeathWeaponLineage(
    const RuntimeProjectileEffect& projectile) {
    return projectile.deathWeaponCascadeId != 0
        && projectile.deathWeaponRequestId != 0;
}

bool runtimeProjectileHasLockedDeathWeaponDamage(
    const RuntimeProjectileEffect& projectile) {
    // A DeathWeapon request stores the already modified authored damage in
    // projectile.damage.  Only its root projectile is locked; derived debris
    // and shrapnel keep normal authored behavior while retaining the cause.
    return runtimeProjectileHasDeathWeaponLineage(projectile)
        && projectile.parentActorId < 0
        && projectile.generation == 0;
}

RuntimeDeathWeaponCause runtimeDeathWeaponCauseForProjectile(
    const RuntimeProjectileEffect& projectile) {
    if (!runtimeProjectileHasDeathWeaponLineage(projectile)) {
        return {};
    }
    return {
        projectile.deathWeaponCascadeId,
        projectile.deathWeaponRequestId,
        projectile.deathWeaponCascadeDepth
    };
}

double runtimeProjectilePercentAtMaxFraction(
    const RuntimeProjectileEffect& projectile) {
    if (projectile.percentAtMax <= 0.0) {
        return 1.0;
    }
    if (projectile.percentAtMax > 1.0) {
        return std::clamp(
            projectile.percentAtMax / 100.0,
            0.0,
            1.0
        );
    }
    return std::clamp(projectile.percentAtMax, 0.0, 1.0);
}

const BuildingType* runtimeOverlayWallRuleForCell(
    const RuntimeGameSessionState& state, const TerrainCell& cell) {
    const auto& wall = cell.overlayWall;
    if (wall.health < 0 || wall.originalIndex < 0 || wall.originalType.empty()) return nullptr;
    const auto& overlays = state.rules.getOverlayTypes();
    if (wall.originalIndex >= static_cast<int>(overlays.size())) return nullptr;
    const auto& overlay = overlays[static_cast<std::size_t>(wall.originalIndex)];
    if (!overlay.wall || toUpperAsciiLocal(overlay.id) != toUpperAsciiLocal(wall.originalType)) return nullptr;
    const auto* building = findRuntimeBuildingTypeById(state.rules, wall.originalType);
    return building && building->wall && building->strength > 0
        && wall.health <= building->strength ? building : nullptr;
}

bool runtimeOverlayWallUsesCurrentOverlay(const TerrainCell& cell) {
    return cell.hasOverlay && cell.overlayIndex == cell.overlayWall.originalIndex
        && (cell.overlayType.empty()
            || toUpperAsciiLocal(cell.overlayType) == toUpperAsciiLocal(cell.overlayWall.originalType));
}

bool runtimeIsOverlayWallTargetId(int id) {
    constexpr int first = std::numeric_limits<int>::min();
    return id >= first && id <= first + 512 * 512 - 1;
}

bool runtimeOverlayWallBehaviorTargetIsValid(
    const RuntimeGameSessionState& state, const RuntimeObjectBehavior& behavior) {
    if (!runtimeIsOverlayWallTargetId(behavior.targetObjectId)) return true;
    if (behavior.kind != RuntimeBehaviorKind::Attack || !behavior.forceFire
        || !behavior.attackGround || behavior.attackBridge) return false;
    const auto point = behavior.attackBridgeCell;
    if (point.x < 0 || point.x >= 512 || point.y < 0 || point.y >= 512
        || behavior.targetObjectId - std::numeric_limits<int>::min() != point.y * 512 + point.x)
        return false;
    for (const auto& cell : state.world.getTerrainCells())
        if (cell.x == point.x && cell.y == point.y)
            // A wall can die after firing while the command is still awaiting its next tick.
            return runtimeOverlayWallRuleForCell(state, cell) != nullptr
                && (cell.overlayWall.health == 0 || runtimeOverlayWallUsesCurrentOverlay(cell));
    return false;
}

std::optional<WorldObjectInstance> runtimeOverlayWallTargetAtCell(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& point) {
    if (point.x < 0 || point.x >= 512 || point.y < 0 || point.y >= 512) return std::nullopt;
    const int key = runtimeCellKey(point.x, point.y);
    const TerrainCell* cell = nullptr;
    const auto found = state.terrainByCell.find(key);
    if (found != state.terrainByCell.end()) cell = found->second;
    if (!cell) for (const auto& candidate : state.world.getTerrainCells())
        if (candidate.x == point.x && candidate.y == point.y) { cell = &candidate; break; }
    if (!cell || cell->overlayWall.health <= 0 || !runtimeOverlayWallRuleForCell(state, *cell)
        || !runtimeOverlayWallUsesCurrentOverlay(*cell)) return std::nullopt;
    WorldObjectInstance target;
    target.instanceId = std::numeric_limits<int>::min() + point.y * 512 + point.x;
    target.kind = WorldObjectKind::Structure;
    target.mapId = "OverlayWall:" + std::to_string(key);
    target.type = cell->overlayWall.originalType;
    target.owner = "RuntimeMapWall";
    target.x = point.x; target.y = point.y; target.cell = key; target.level = cell->level;
    target.health = cell->overlayWall.health;
    target.alive = target.poweredOperational = true;
    return target;
}

std::optional<WorldObjectInstance> runtimeAttackGroundTargetObject(
    const RuntimeGameSessionState& state, const RuntimeObjectBehavior& behavior) {
    if (!behavior.attackGround) return std::nullopt;
    if (runtimeIsOverlayWallTargetId(behavior.targetObjectId)) {
        const int key = behavior.targetObjectId - std::numeric_limits<int>::min();
        if (behavior.attackBridgeCell.x < 0 || behavior.attackBridgeCell.x >= 512 || behavior.attackBridgeCell.y < 0 || behavior.attackBridgeCell.y >= 512 || key != behavior.attackBridgeCell.y * 512 + behavior.attackBridgeCell.x)
            return std::nullopt;
        return runtimeOverlayWallTargetAtCell(state, behavior.attackBridgeCell);
    }
    return runtimeGroundAttackTargetObject(state, behavior.attackBridgeCell);
}

void refreshRuntimeOverlayWallTopology(RuntimeGameSessionState& state) {
    std::unordered_map<int, TerrainCell*> tracked;
    std::unordered_set<int> playable;
    for (auto& cell : state.world.accessTerrainCells()) {
        const int key = runtimeCellKey(cell.x, cell.y);
        playable.insert(key);
        if (runtimeOverlayWallRuleForCell(state, cell)) tracked.emplace(key, &cell);
    }
    constexpr std::array<std::array<int, 3>, 4> directions{{
        {{0, -1, 1}}, {{1, 0, 2}}, {{0, 1, 4}}, {{-1, 0, 8}}
    }};
    for (const auto& [key, pointer] : tracked) {
        (void)key;
        auto& cell = *pointer;
        auto& wall = cell.overlayWall;
        if (wall.health == 0) {
            if (runtimeOverlayWallUsesCurrentOverlay(cell)) {
                cell.hasOverlay = false; cell.overlayIndex = -1; cell.overlayFrame = 0;
                cell.overlayType.clear(); cell.overlayImage.clear();
                cell.overlayIsOre = cell.overlayIsGem = false;
            }
            continue;
        }
        if (!runtimeOverlayWallUsesCurrentOverlay(cell)) continue;
        // A border neighbor absent from the playable terrain retains its authored bit.
        int mask = wall.originalFrame & 15;
        for (const auto& direction : directions) {
            const int x = cell.x + direction[0], y = cell.y + direction[1];
            if (x < 0 || x >= 512 || y < 0 || y >= 512) continue;
            const int neighborKey = runtimeCellKey(x, y);
            const auto neighbor = tracked.find(neighborKey);
            if (neighbor == tracked.end()) {
                if (playable.contains(neighborKey)) mask &= ~direction[2];
                continue;
            }
            const auto& adjacent = *neighbor->second;
            const bool connected = adjacent.overlayWall.health > 0
                && runtimeOverlayWallUsesCurrentOverlay(adjacent)
                && toUpperAsciiLocal(adjacent.overlayWall.originalType) == toUpperAsciiLocal(wall.originalType);
            mask = connected ? mask | direction[2] : mask & ~direction[2];
        }
        WorldObjectInstance artwork;
        artwork.kind = WorldObjectKind::Structure; artwork.type = wall.originalType;
        const auto* art = runtimeArtTypeForObject(state, artwork);
        const int levels = std::max(1, art ? art->damageLevels : 1);
        const int strength = runtimeOverlayWallRuleForCell(state, cell)->strength;
        int bank = 0;
        if (static_cast<long long>(wall.health) * 4 <= strength) bank = std::min(2, levels - 1);
        else if (static_cast<long long>(wall.health) * 2 <= strength) bank = std::min(1, levels - 1);
        // Extra authored banks are not classified by DamageLevels. Preserve their
        // original artwork while HP is positive; representative bootstrap HP is
        // an engine policy, not evidence of retail exact wall health.
        const int authoredBank = wall.originalFrame / 16;
        if (authoredBank >= levels) bank = authoredBank;
        cell.overlayFrame = mask + bank * 16;
    }
}

RuntimeTerrainProjectileDamageResult applyRuntimeProjectileOverlayWallDamage(
    RuntimeProductionHostState& state, const RuntimeProjectileEffect& projectile,
    const RuntimeCellPoint& impactCell, const WeaponType* weapon, const WarheadType* warhead) {
    RuntimeTerrainProjectileDamageResult result;
    if (!projectile.ag || !warhead || (!warhead->wall && !warhead->wood)
        || warhead->mindControl || warhead->electricAssault || warhead->temporal
        || warhead->ivanBomb || warhead->bombDisarm) return result;
    const double radius = std::max(0.0, projectile.cellSpread);
    std::vector<int> changed;
    for (auto& cell : state.world.accessTerrainCells()) {
        if (cell.overlayWall.health <= 0 || !runtimeOverlayWallRuleForCell(state, cell)
            || !runtimeOverlayWallUsesCurrentOverlay(cell)) continue;
        const double distance = runtimeCellDistance(impactCell, {cell.x, cell.y});
        if ((radius <= 0 && distance > 0) || (radius > 0 && distance > radius)) continue;
        auto target = runtimeOverlayWallTargetAtCell(state, {cell.x, cell.y});
        if (!target || !runtimeProjectileCanHitTarget(state, projectile, *target)) continue;
        long long baseDamage = projectile.damage;
        std::string damageWarhead = projectile.warheadId;
        if (projectile.spawnedMissileDamage) baseDamage = projectile.spawnedMissileBaseDamage;
        else if (!projectile.particleDamagePulse && !runtimeProjectileHasLockedDeathWeaponDamage(projectile) && weapon) {
            baseDamage = runtimeCombatProjectileAuthoredDamage(state, projectile, *weapon);
            damageWarhead = weapon->warhead;
        }
        if (baseDamage <= 0) continue;
        const int damage = runtimeAuthoredDamageAgainst(state, *target, baseDamage, damageWarhead,
            distance, projectile.veteranDamageMultiplier);
        if (damage <= 0) continue;
        const int before = cell.overlayWall.health;
        cell.overlayWall.health = static_cast<int>(std::max(0LL, static_cast<long long>(before) - damage));
        ++result.objectsDamaged;
        result.totalDamage += before - cell.overlayWall.health;
        if (cell.overlayWall.health == 0) ++result.objectsDestroyed;
        changed.push_back(runtimeCellKey(cell.x, cell.y));
    }
    if (changed.empty()) return result;
    refreshRuntimeOverlayWallTopology(state);
    for (int key : changed) {
        const auto found = state.terrainByCell.find(key);
        if (found != state.terrainByCell.end() && found->second)
            state.terrainInfoByCell[key] = runtimeNavigationTerrainInfoForCell(state, *found->second);
    }
    state.attackApproachCache.clear(); state.pathFailureCache.clear();
    state.pathTerrainCache.valid = false; state.pathTerrainCache.terrainCellCount = 0;
    state.pathTerrainCache.staticCells.clear(); state.pathTerrainCache.dynamicValid = false;
    state.pathTerrainCache.dynamicCells.clear();
    invalidateRuntimeTickOccupancyCache(state); invalidateRuntimeObjectProjectionCache(state);
    state.renderCache.terrainLayer = RuntimeTerrainLayerCache{};
    state.placementChecksCacheTick = -1;
    state.radarTerrainKey.clear(); state.radarProjectionCacheValid = false;
    state.navigationRefreshPending = true;
    state.repaintRequested = true;
    return result;
}

RuntimePsychicStatusHitResult applyRuntimePsychicProjectileHit(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& target,
    const WarheadType& warhead) {
    RuntimePsychicStatusHitRequest request;
    request.currentTick = state.tick;
    request.hitEventId = static_cast<std::uint64_t>(std::max(
        0,
        projectile.actorId
    ));
    request.sourceValidatedAtLaunch = projectile.launchSourceValidated;
    request.source.objectId = projectile.attackerObjectId;
    request.source.owner = projectile.owner;
    request.source.typeId = projectile.launchSourceType;
    request.source.kind = runtimePsychicKindForLaunchSource(
        projectile.launchSourceKind,
        projectile.launchSourceAuthoredAircraft
    );
    if (const WorldObjectInstance* liveSource =
            findRuntimeObjectByIdIncludingInactive(
                state,
                projectile.attackerObjectId
            )) {
        request.source.present = true;
        request.source.alive = liveSource->alive && liveSource->health > 0;
    }
    request.target = runtimePsychicObjectSnapshot(state, target);
    request.warhead.warheadId = warhead.id;
    request.warhead.warheadExists = true;
    request.warhead.psychedelic = warhead.psychedelic;
    const WeaponType* authoredWeapon = findRuntimeWeaponType(
        state.rules,
        projectile.weaponId
    );
    const int psychicStatusLegacyFrames =
        runtimeProjectileHasLockedDeathWeaponDamage(projectile)
            ? projectile.damage
            : (projectile.spawnedMissileDamage
                ? projectile.spawnedMissileBaseDamage
                : (authoredWeapon == nullptr
                    ? projectile.damage
                    : authoredWeapon->damage));
    if (!runtimePsychicLegacyFramesToFixedTicks(
            psychicStatusLegacyFrames,
            std::max(1, state.config.fixedTickHz),
            request.durationTicks
        )) {
        return {};
    }
    RuntimePsychicStatusSystemState psychicCandidate =
        state.psychicStatuses;
    RuntimePsychicStatusHitResult result = psychicCandidate.applyHit(request);
    if (result.accepted && !result.idempotent) {
        const bool applied = std::any_of(
            result.actions.begin(),
            result.actions.end(),
            [](const RuntimePsychicStatusAction& action) {
                return action.kind
                    == RuntimePsychicStatusActionKind::Applied;
            }
        );
        if (applied && !releaseRuntimePsychicAppliedAuthorities(
                state,
                target.instanceId
            )) {
            state.simulationStatus =
                "psychic status authority release rejected atomically";
            return {};
        }
        state.psychicStatuses = std::move(psychicCandidate);
        applyRuntimePsychicStatusActions(state, result.actions);
    }
    return result;
}

bool applyRuntimeMagBeamFallDamage(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    int damage,
    const RuntimeMagBeamAction& action,
    const std::string& reason) {
    if (!object.alive || object.health <= 0 || damage <= 0
        || runtimeObjectIgnoresDamage(state, object)
        || (state.superWeaponsInitialized
            && state.superWeapons.isObjectProtected(
                object.instanceId,
                state.tick
            ))) {
        return false;
    }
    object.health = std::max(0, object.health - damage);
    runtimeRevealObjectAfterDamage(state, object, "magBeamFall");
    if (object.health > 0) {
        return true;
    }
    RuntimeObjectExitRequest objectExit;
    objectExit.objectId = object.instanceId;
    objectExit.kind = RuntimeObjectExitKind::Destroyed;
    objectExit.tick = state.tick;
    objectExit.mission = "Destroyed";
    objectExit.reason = reason;
    objectExit.sourceObjectId = action.sourceObjectId;
    objectExit.sourceOwner = action.sourceOwner;
    (void)queueRuntimeObjectExit(state, std::move(objectExit));
    return true;
}

void resolveRuntimeMagBeamRenderEndpoints(RuntimeProductionHostState& state,
    SpecialMechanismRenderSnapshot& snapshot, const RuntimeMagBeamBindingState& binding) {
    const auto* source = findObjectById(state, binding.sourceObjectId);
    const auto* target = findObjectById(state, binding.targetObjectId);
    if (!source || !target) return;
    const auto sourceOrder = state.behaviors.find(binding.sourceObjectId);
    const bool profileForceFire = sourceOrder != state.behaviors.end()
        && sourceOrder->second.kind == RuntimeBehaviorKind::Attack
        && sourceOrder->second.targetObjectId == binding.targetObjectId
        && sourceOrder->second.forceFire;
    const auto profile = runtimeWeaponProfileForObject(state, *source, target, profileForceFire);
    if (!profile.weapon) return;
    auto muzzle = runtimeProjectileSourcePosition(state, *source, profile,
        runtimeWeaponFiringFacing(state, *source, *target));
    alignRuntimeWeaponMuzzleToSprite(state, *source, profile, runtimeWeaponFiringFacing(state, *source, *target), muzzle);
    snapshot.sourceX = muzzle.x; snapshot.sourceY = muzzle.y; snapshot.sourceLevel = muzzle.z;
    snapshot.sourceScreenOffsetX = muzzle.screenOffsetX;
    snapshot.sourceNativeFixedX = muzzle.nativeFixedX;
    snapshot.sourceScreenOffsetY = muzzle.screenOffsetY;
    snapshot.sourceNativeFixedY = muzzle.nativeFixedY;
    snapshot.targetX = binding.kinematics.worldX;
    snapshot.targetY = binding.kinematics.worldY;
    snapshot.targetLevel = binding.kinematics.worldZ;
    snapshot.color = state.rules.getAudioVisualValue("MagnaBeamColor");
    snapshot.hasBeamEndpoints = true;
}

bool advanceRuntimeMagBeamVisual(RuntimeProductionHostState& state, RuntimeProjectileEffect& beam) {
    const auto* source = findObjectById(state, beam.attackerObjectId);
    const auto* target = findObjectById(state, beam.targetObjectId);
    const auto order = state.behaviors.find(beam.attackerObjectId);
    if (!source || !target || source->health <= 0 || target->health <= 0
        || source->owner != beam.owner || source->renderMoveActive
        || order == state.behaviors.end() || order->second.kind != RuntimeBehaviorKind::Attack
        || order->second.targetObjectId != target->instanceId
        || (order->second.state != RuntimeBehaviorState::Attacking
            && order->second.state != RuntimeBehaviorState::Cooldown)) return false;
    const auto profile = runtimeWeaponProfileForObject(state, *source, target, order->second.forceFire);
    if (!profile.weapon || !profile.weapon->magBeam || !profile.warhead || profile.warhead->isLocomotor
        || toLowerAsciiLocal(profile.weapon->id) != toLowerAsciiLocal(beam.weaponId)
        || runtimeWeaponDistanceToTarget(state, *source, *target, *profile.weapon)
            > runtimeEffectiveWeaponRange(state, *source, *target, *profile.weapon)
        || !runtimeWeaponHasLineOfSight(state, *source, *target, *profile.weapon)) return false;
    // A single presentation actor follows this firing order. Refreshing its
    // deadline never applies damage or restarts the weapon's reload clock.
    syncRuntimeProjectileTargetEndpoint(state, beam);
    auto muzzle = runtimeProjectileSourcePosition(state, *source, profile,
        runtimeWeaponFiringFacing(state, *source, *target));
    alignRuntimeWeaponMuzzleToSprite(state, *source, profile, runtimeWeaponFiringFacing(state, *source, *target), muzzle);
    beam.hasSourcePosition = true;
    beam.sourceX = muzzle.x; beam.sourceY = muzzle.y; beam.sourceZ = muzzle.z;
    beam.sourceScreenOffsetX = muzzle.screenOffsetX;
    beam.sourceNativeFixedX = muzzle.nativeFixedX;
    beam.sourceScreenOffsetY = muzzle.screenOffsetY;
    beam.sourceNativeFixedY = muzzle.nativeFixedY;
    beam.fromCell = runtimeCellPointForObject(*source); beam.fromLevel = source->level;
    beam.elapsedTicks = std::max(0, state.tick - beam.spawnTick);
    beam.totalTicks = beam.elapsedTicks + 2; beam.remainingTicks = 2;
    beam.impactTick = state.tick + 2;
    beam.lifecycle = RuntimeProjectileLifecycleState::Traveling;
    beam.lifecycleState = runtimeProjectileLifecycleName(beam.lifecycle);
    updateRuntimeProjectileTravelPosition(beam);
    return true;
}

void spawnRuntimeMagBeamVisual(RuntimeProductionHostState& state, const RuntimeProjectileEffect& shot) {
    if (shot.renderIntent == "magBeamVisual") return;
    const auto* weapon = state.rules.findWeaponType(shot.weaponId);
    const auto* warhead = state.rules.findWarheadType(shot.warheadId);
    // The locomotor owns vehicle links. A building firing order owns one
    // persistent presentation actor, independent of its periodic damage.
    if (!weapon || !weapon->magBeam || !warhead || warhead->isLocomotor) return;
    const auto sameLink = [&](const RuntimeProjectileEffect& actor) {
        return actor.renderIntent == "magBeamVisual" && actor.remainingTicks > 0
            && actor.attackerObjectId == shot.attackerObjectId
            && actor.targetObjectId == shot.targetObjectId && actor.weaponId == shot.weaponId;
    };
    if (std::any_of(state.projectiles.begin(), state.projectiles.end(), sameLink)
        || std::any_of(state.projectileTickScratch.begin(), state.projectileTickScratch.end(), sameLink)) return;
    RuntimeProjectileEffect beam = shot;
    beam.actorId = nextRuntimeProjectileActorId(state);
    beam.parentActorId = shot.actorId;
    beam.generation = std::min(4, shot.generation + 1);
    beam.renderIntent = "magBeamVisual";
    beam.sourceAnchorObjectId = -1; // FLH is refreshed explicitly, not the body center.
    beam.projectileId.clear(); beam.projectileImageId.clear(); beam.impactAnimId.clear();
    beam.weaponAnimId.clear(); beam.reportSoundId.clear();
    beam.damage = 0; beam.laser = true; beam.electricBolt = false;
    beam.totalTicks = 2;
    beam.remainingTicks = beam.totalTicks; beam.elapsedTicks = 0;
    beam.spawnTick = state.tick; beam.impactTick = state.tick + beam.totalTicks;
    beam.lifecycle = RuntimeProjectileLifecycleState::Spawned;
    beam.lifecycleState = runtimeProjectileLifecycleName(beam.lifecycle);
    if (!advanceRuntimeMagBeamVisual(state, beam)) return;
    recordRuntimeProjectileReplayEvent(state, beam, "ProjectileSpawned");
    state.projectiles.push_back(std::move(beam));
}

bool runtimeMagBeamWarheadCombinationIsExclusive(
    const RuntimeProjectileEffect& projectile,
    const WeaponType& weapon,
    const WarheadType& warhead) {
    return warhead.isLocomotor
        && !warhead.psychedelic
        && !warhead.parasite
        && !warhead.airstrike
        && !weapon.limboLaunch
        && !projectile.particleDamagePulse
        && !projectile.suppressWarheadParticleSpawn
        && trimAsciiLocal(warhead.particle).empty()
        && trimAsciiLocal(warhead.particleSystem).empty();
}

bool applyRuntimeMagBeamProjectileImpactExclusive(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const WeaponType& weapon,
    const WarheadType& warhead) {
    recordRuntimeProjectileReplayEvent(
        state,
        projectile,
        "MagBeamImpact"
    );
    ++state.actionSummary.impactsThisTick;
    if (!runtimeMagBeamWarheadCombinationIsExclusive(
            projectile,
            weapon,
            warhead
        )) {
        state.simulationStatus = "mag beam impact rejected: mixed mechanism";
        return true;
    }
    const RuntimeMagBeamBindingState* sourceBinding =
        runtimeMagBeamBindingForSource(
            state,
            projectile.attackerObjectId
        );
    if (sourceBinding != nullptr) {
        const bool keepalive =
            sourceBinding->targetObjectId == projectile.targetObjectId
            && toLowerAsciiLocal(trimAsciiLocal(sourceBinding->weaponId))
                == toLowerAsciiLocal(trimAsciiLocal(weapon.id))
            && toLowerAsciiLocal(trimAsciiLocal(sourceBinding->warheadId))
                == toLowerAsciiLocal(trimAsciiLocal(warhead.id))
            && sourceBinding->phase != RuntimeMagBeamPhase::Falling;
        state.simulationStatus = keepalive
            ? "mag beam binding keepalive"
            : "mag beam source already owns a binding";
        return true;
    }
    WorldObjectInstance* source = findMutableObjectById(
        state,
        projectile.attackerObjectId
    );
    WorldObjectInstance* target = findMutableObjectById(
        state,
        projectile.targetObjectId
    );
    if (source == nullptr || target == nullptr
        || !source->alive || source->health <= 0
        || !target->alive || target->health <= 0
        || runtimeObjectManagedByAirstrike(state, source->instanceId)
        || runtimeObjectManagedByAirstrike(state, target->instanceId)
        || runtimeObjectSuspendedByMagBeam(
            state,
            source->instanceId
        )) {
        state.simulationStatus = "mag beam impact vetoed: missing actor";
        return true;
    }

    if (state.specialMechanismsInitialized
        && (state.specialMechanisms.isChronoInTransit(target->instanceId)
            || state.specialMechanisms.isTemporalFrozen(
                target->instanceId
            )
            || state.specialMechanisms.isCarrierChild(target->instanceId))) {
        state.simulationStatus =
            "mag beam impact vetoed: displacement authority conflict";
        return true;
    }
    const auto behavior = state.behaviors.find(source->instanceId);
    const bool profileForceFire = behavior != state.behaviors.end()
        && behavior->second.kind == RuntimeBehaviorKind::Attack
        && behavior->second.targetObjectId == target->instanceId
        && behavior->second.forceFire;
    const RuntimeWeaponProfile profile = runtimeWeaponProfileForObject(
        state,
        *source,
        target,
        profileForceFire
    );
    if (behavior == state.behaviors.end()
        || behavior->second.kind != RuntimeBehaviorKind::Attack
        || behavior->second.targetObjectId != target->instanceId
        || profile.weapon == nullptr || profile.warhead == nullptr
        || toLowerAsciiLocal(trimAsciiLocal(profile.weapon->id))
            != toLowerAsciiLocal(trimAsciiLocal(weapon.id))
        || toLowerAsciiLocal(trimAsciiLocal(profile.warhead->id))
            != toLowerAsciiLocal(trimAsciiLocal(warhead.id))) {
        state.simulationStatus = "mag beam impact vetoed: source lock ended";
        return true;
    }

    RuntimeMagBeamAcquireRequest request;
    request.currentTick = state.tick;
    // Only the still-live order authorizes friendly acquisition. In particular,
    // a discarded Ctrl order or a berserk targeting veto grants no permission.
    request.forceFire = behavior->second.forceFire
        && runtimePsychicCanAttackTarget(state, *source, *target, true, true);
    request.source.objectId = source->instanceId;
    request.source.owner = source->owner;
    request.source.typeId = source->type;
    request.source.present = true;
    request.source.alive = true;
    request.target = runtimeMagBeamTargetSnapshotForObject(
        state,
        *target,
        source,
        warhead.locomotor,
        nullptr
    );
    request.weapon.weaponId = weapon.id;
    request.weapon.warheadId = warhead.id;
    request.weapon.locomotorId = trimAsciiLocal(warhead.locomotor);
    request.weapon.weaponExists = true;
    request.weapon.isMagBeam = weapon.magBeam;
    request.weapon.warheadExists = true;
    request.weapon.isLocomotor = warhead.isLocomotor;
    request.weapon.rawDamage = weapon.damage;
    const SpecialMechanismRules& rules =
        state.rules.getSpecialMechanismRules();
    request.rules.fallingDamageMultiplier = rules.fallingDamageMultiplier;
    request.rules.currentStrengthDamage = rules.currentStrengthDamage;

    RuntimeMagBeamSystemState candidate = state.magBeams;
    const RuntimeMagBeamAcquireResult acquired = candidate.acquire(request);
    if (!acquired.accepted) {
        state.simulationStatus = std::string("mag beam impact vetoed: ")
            + runtimeMagBeamVetoName(acquired.veto);
        return true;
    }
    if (!acquired.idempotent) {
        // Parasite Teleporting must be known-valid before any dock/repair
        // authority is released.  That preserves the projectile impact as an
        // all-or-nothing displacement boundary when no legal eject cell
        // exists.
        if (!preflightRuntimeParasiteHostEvent(
                state,
                target->instanceId,
                ParasiteHostEventKind::Teleporting,
                0
            )) {
            state.simulationStatus =
                "mag beam impact vetoed: parasite teleport conflict";
            return true;
        }
        const RuntimeRepairDepotSystemState repairBefore =
            state.repairDepots;
        const RuntimeFactoryDeliveryState deliveriesBefore =
            state.factoryDeliveries;
        const RuntimeAircraftServiceSystemState aircraftServicesBefore =
            state.aircraftServices;
        const RuntimeSpecialMechanismState specialMechanismsBefore =
            state.specialMechanisms;
        const RuntimeSubcellSystemState subcellsBefore = state.subcells;
        const std::vector<WorldObjectInstance> worldObjectsBefore =
            state.world.getObjects();
        const std::vector<int> selectedBefore = state.selectedObjectIds;
        const auto behaviorsBefore = state.behaviors;
        const auto pendingPathsBefore = state.pendingPathObjectIds;
        const auto engineerCapturesBefore =
            state.pendingEngineerCaptures;
        const auto engineerActionsBefore = state.pendingEngineerActions;
        const auto occupyTargetsBefore = state.pendingOccupyTargets;
        const auto transportTargetsBefore = state.pendingTransportTargets;
        const auto timedBombIntentsBefore =
            state.pendingTimedBombIntents;
        const auto parasiteStunsBefore =
            state.parasiteStunUntilTickByObject;
        const auto pendingObjectExitsBefore = state.pendingObjectExits;
        const auto pendingObjectExitIdsBefore = state.pendingObjectExitIds;
        const bool navigationRefreshBefore =
            state.navigationRefreshPending;
        const bool repaintBefore = state.repaintRequested;
        const auto rollbackAuthorities = [&]() {
            state.repairDepots = repairBefore;
            state.factoryDeliveries = deliveriesBefore;
            state.aircraftServices = aircraftServicesBefore;
            state.specialMechanisms = specialMechanismsBefore;
            state.subcells = subcellsBefore;
            state.world.accessObjects() = worldObjectsBefore;
            state.selectedObjectIds = selectedBefore;
            state.behaviors = behaviorsBefore;
            state.pendingPathObjectIds = pendingPathsBefore;
            state.pendingEngineerCaptures = engineerCapturesBefore;
            state.pendingEngineerActions = engineerActionsBefore;
            state.pendingOccupyTargets = occupyTargetsBefore;
            state.pendingTransportTargets = transportTargetsBefore;
            state.pendingTimedBombIntents = timedBombIntentsBefore;
            state.parasiteStunUntilTickByObject = parasiteStunsBefore;
            state.pendingObjectExits = pendingObjectExitsBefore;
            state.pendingObjectExitIds = pendingObjectExitIdsBefore;
            state.navigationRefreshPending = navigationRefreshBefore;
            state.repaintRequested = repaintBefore;
            invalidateRuntimeTickOccupancyCache(state);
            invalidateRuntimeObjectProjectionCache(state);
        };
        if (!releaseRuntimeExclusiveAppliedAuthorities(
                state,
                target->instanceId,
                "mag beam applied",
                true
            )) {
            rollbackAuthorities();
            state.simulationStatus =
                "mag beam authority release rejected atomically";
            return true;
        }
        if (!handleRuntimeParasiteHostEvent(
                state,
                target->instanceId,
                ParasiteHostEventKind::Teleporting,
                0
            )) {
            // The exact operation was preflighted against the same tick and
            // world. Treat any divergence as a hard veto and never install
            // the MagBeam authority.
            rollbackAuthorities();
            state.simulationStatus =
                "mag beam parasite teleport commit rejected unexpectedly";
            return true;
        }
        state.magBeams = std::move(candidate);
        (void)applyRuntimeMagBeamActions(state, acquired.actions);
    }
    state.simulationStatus = "mag beam target acquired";
    return true;
}

RuntimeAirstrikeBounds runtimeAirstrikeWorldBounds(
    const RuntimeGameSessionState& state) {
    RuntimeAirstrikeBounds bounds;
    const std::vector<TerrainCell>& terrain = state.world.getTerrainCells();
    if (terrain.empty()) {
        return bounds;
    }
    bounds.minX = terrain.front().x;
    bounds.maxX = terrain.front().x;
    bounds.minY = terrain.front().y;
    bounds.maxY = terrain.front().y;
    for (const TerrainCell& cell : terrain) {
        bounds.minX = std::min(bounds.minX, cell.x);
        bounds.maxX = std::max(bounds.maxX, cell.x);
        bounds.minY = std::min(bounds.minY, cell.y);
        bounds.maxY = std::max(bounds.maxY, cell.y);
    }
    return bounds;
}

RuntimeAirstrikeAircraftDefinition runtimeAirstrikeAircraftDefinition(
    const RuntimeGameSessionState& state,
    const std::string& typeId,
    bool elite) {
    RuntimeAirstrikeAircraftDefinition definition;
    definition.typeId = trimAsciiLocal(typeId);
    const UnitType* aircraft = state.rules.findUnitType(
        "AircraftType",
        definition.typeId
    );
    const UnitType* ambiguousVehicle = state.rules.findUnitType(
        "UnitType",
        definition.typeId
    );
    definition.exists = aircraft != nullptr && ambiguousVehicle == nullptr;
    definition.isAircraft = definition.exists;
    if (!definition.exists) {
        return definition;
    }
    definition.weaponId = elite && !aircraft->elitePrimaryWeapon.empty()
        ? aircraft->elitePrimaryWeapon
        : aircraft->primaryWeapon;
    const WeaponType* weapon = findRuntimeWeaponType(
        state.rules,
        definition.weaponId
    );
    const WarheadType* warhead = weapon == nullptr
        ? nullptr
        : findRuntimeWarheadType(state.rules, weapon->warhead);
    definition.hasAttackWeapon = weapon != nullptr
        && warhead != nullptr
        && !warhead->airstrike
        && !warhead->parasite
        && !warhead->psychicDamage
        && !warhead->psychedelic
        && trimAsciiLocal(warhead->particle).empty()
        && trimAsciiLocal(warhead->particleSystem).empty()
        && !weapon->limboLaunch
        && !weapon->spawner;
    if (definition.hasAttackWeapon) {
        definition.weaponId = weapon->id;
        definition.warheadId = warhead->id;
    }
    return definition;
}

bool runtimeAirstrikeDesignatorCombinationIsExclusive(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WeaponType& weapon,
    const WarheadType& warhead) {
    const ProjectileType* projectileRules = findRuntimeProjectileType(
        state.rules,
        projectile.projectileId
    );
    return warhead.airstrike
        && weapon.migAttackCursor
        && projectileRules != nullptr
        && !warhead.parasite
        && !warhead.psychicDamage
        && !warhead.psychedelic
        && !warhead.mindControl
        && !warhead.temporal
        && !warhead.radiation
        && !warhead.electricAssault
        && !warhead.ivanBomb
        && !warhead.bombDisarm
        && !warhead.isLocomotor
        && !warhead.tiberium
        && !warhead.culling
        && warhead.paralyzes == 0
        && trimAsciiLocal(warhead.particle).empty()
        && trimAsciiLocal(warhead.particleSystem).empty()
        && !weapon.sonic
        && !weapon.electricBolt
        && !weapon.radBeam
        && !weapon.magBeam
        && !weapon.spawner
        && !weapon.limboLaunch
        && !weapon.infiniteMindControl
        && !weapon.permanentMindControl
        && !weapon.isRadEruption
        && !projectileRules->airburst
        && projectileRules->shrapnelCount == 0
        && trimAsciiLocal(projectileRules->shrapnelWeapon).empty()
        && projectile.parentActorId == -1
        && projectile.generation == 0
        && projectile.sourceAnchorObjectId < 0
        && projectile.deathWeaponCascadeId == 0
        && projectile.deathWeaponRequestId == 0
        && projectile.deathWeaponCascadeDepth == 0
        && projectile.airstrikeStrikeId == 0
        && projectile.airstrikePlaneToken == 0
        && !projectile.particleDamagePulse
        && !projectile.suppressWarheadParticleSpawn;
}

void completeRuntimeAirstrikeDesignatorBehavior(
    RuntimeProductionHostState& state,
    int callerObjectId,
    int targetObjectId) {
    const auto behavior = state.behaviors.find(callerObjectId);
    if (behavior == state.behaviors.end()
        || behavior->second.kind != RuntimeBehaviorKind::Attack
        || behavior->second.targetObjectId != targetObjectId) {
        return;
    }
    behavior->second.state = RuntimeBehaviorState::Complete;
    behavior->second.weaponCooldownTicks = 0;
    if (WorldObjectInstance* caller = findMutableObjectById(
            state,
            callerObjectId
        )) {
        caller->mission = "Guard";
    }
    ++state.actionSummary.completedCommands;
}

void resolveRuntimeAirstrikeRenderEndpoints(RuntimeProductionHostState& state,
    SpecialMechanismRenderSnapshot& snapshot) {
    const auto* caller = findObjectById(state, snapshot.objectId);
    const auto* target = findObjectById(state, snapshot.targetObjectId);
    if (!caller || !target) return;
    const auto targetPosition = runtimeProjectileTargetPosition(state, *target);
    snapshot.targetX = targetPosition.x; snapshot.targetY = targetPosition.y;
    snapshot.targetLevel = targetPosition.level;
    const auto sourceOrder = state.behaviors.find(snapshot.objectId);
    const bool profileForceFire = sourceOrder != state.behaviors.end()
        && sourceOrder->second.kind == RuntimeBehaviorKind::Attack
        && sourceOrder->second.targetObjectId == snapshot.targetObjectId
        && sourceOrder->second.forceFire;
    const auto profile = runtimeWeaponProfileForObject(state, *caller, target, profileForceFire);
    if (!profile.weapon) return;
    auto muzzle = runtimeProjectileSourcePosition(state, *caller, profile,
        runtimeWeaponFiringFacing(state, *caller, *target));
    alignRuntimeWeaponMuzzleToSprite(state, *caller, profile, runtimeWeaponFiringFacing(state, *caller, *target), muzzle);
    snapshot.sourceX = muzzle.x; snapshot.sourceY = muzzle.y; snapshot.sourceLevel = muzzle.z;
    snapshot.sourceScreenOffsetX = muzzle.screenOffsetX;
    snapshot.sourceNativeFixedX = muzzle.nativeFixedX;
    snapshot.sourceScreenOffsetY = muzzle.screenOffsetY;
    snapshot.sourceNativeFixedY = muzzle.nativeFixedY;
    snapshot.hasBeamEndpoints = true;
    const auto behavior = state.behaviors.find(caller->instanceId);
    if (behavior == state.behaviors.end() || behavior->second.kind != RuntimeBehaviorKind::Attack
        || behavior->second.state == RuntimeBehaviorState::Complete
        || behavior->second.targetObjectId != target->instanceId) snapshot.cell = {-1,-1};
}

bool applyRuntimeAirstrikeDesignatorImpactExclusive(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const WeaponType& weapon,
    const WarheadType& warhead) {

    const WorldObjectInstance* caller = findObjectById(
        state,
        projectile.attackerObjectId
    );
    const WorldObjectInstance* target = findObjectById(
        state,
        projectile.targetObjectId
    );
    RuntimeAirstrikeRequest request;
    bool requestValid = caller != nullptr
        && target != nullptr
        && caller->health > 0
        && target->health > 0
        && projectile.launchSourceValidated
        && projectile.launchSourceKind == static_cast<int>(caller->kind)
        && toLowerAsciiLocal(trimAsciiLocal(projectile.launchSourceType))
            == toLowerAsciiLocal(trimAsciiLocal(caller->type))
        && projectile.launchSourceAuthoredAircraft == caller->authoredAircraft
        && toLowerAsciiLocal(trimAsciiLocal(projectile.owner))
            == toLowerAsciiLocal(trimAsciiLocal(caller->owner))
        && runtimeAirstrikeDesignatorCombinationIsExclusive(
            state,
            projectile,
            weapon,
            warhead
        );
    if (requestValid) {
        const UnitType* callerRules = findRuntimeUnitType(
            state.rules,
            *caller
        );
        requestValid = callerRules != nullptr;
        if (requestValid) {
            request.callerObjectId = caller->instanceId;
            request.callerOwner = caller->owner;
            request.targetObjectId = target->instanceId;
            const auto attackOrder = state.behaviors.find(caller->instanceId);
            request.forceFire = attackOrder != state.behaviors.end()
                && attackOrder->second.kind == RuntimeBehaviorKind::Attack
                && attackOrder->second.targetObjectId == target->instanceId
                && attackOrder->second.forceFire;
            request.currentTick = state.tick;
            request.elite = caller->veterancy >= 2;
            request.rules.normalAircraftCount = callerRules->airstrikeTeam;
            request.rules.eliteAircraftCount =
                callerRules->eliteAirstrikeTeam;
            request.rules.normalAircraftTypeId =
                callerRules->airstrikeTeamType;
            request.rules.eliteAircraftTypeId =
                callerRules->eliteAirstrikeTeamType;
            request.rules.normalRechargeTicks = runtimeRuleFramesToTicks(
                callerRules->airstrikeRechargeTime, state.config.fixedTickHz);
            request.rules.eliteRechargeTicks = runtimeRuleFramesToTicks(
                callerRules->eliteAirstrikeRechargeTime, state.config.fixedTickHz);
            const std::string& selectedAircraft = request.elite
                ? request.rules.eliteAircraftTypeId
                : request.rules.normalAircraftTypeId;
            request.aircraft = runtimeAirstrikeAircraftDefinition(
                state,
                selectedAircraft,
                request.elite
            );
            request.target.objectId = target->instanceId;
            request.target.cell = {target->x, target->y};
            request.target.alive = target->alive && target->health > 0;
            request.target.visibleToCaller = runtimeObjectVisibleToOwner(
                state,
                caller->owner,
                *target
            );
            request.target.targetable =
                (request.forceFire || !runtimeOwnersAreAllied(state, caller->owner, target->owner))
                && runtimeProjectileCanHitTarget(state, projectile, *target)
                && runtimeWeaponCanTargetObject(state, weapon, *target);
            request.bounds = runtimeAirstrikeWorldBounds(state);
            WorldObjectInstance flight;
            flight.kind = WorldObjectKind::Unit; flight.type = selectedAircraft;
            flight.owner = caller->owner; flight.authoredAircraft = true; flight.airborne = true;
            flight.veterancy = request.elite ? 2 : 0;
            request.flightTicksPerCellQ16 = static_cast<std::int64_t>(std::llround(
                runtimeMovementBaseIntervalTicks(state, flight) * 65536.0));
            if (const RuntimeAirstrikePlaneBinding* binding =
                    runtimeAirstrikeBindingForObject(
                        state,
                        caller->instanceId
                    )) {
                request.sourceStrikeId = binding->strikeId;
                request.sourcePlaneToken = binding->planeToken;
                request.recursionDepth = 1;
            }
        }
    }

    RuntimeAirstrikeRequestResult result;
    if (requestValid) {
        RuntimeAirstrikeSystemState candidate = state.airstrikes;
        if (candidate.nextStrikeId() == 1 && candidate.callers().empty()
            && candidate.strikes().empty() && candidate.projectileReceipts().empty()) {
            auto limits = candidate.limits();
            limits.spawnDelayTicks = runtimeRuleFramesToTicks(1, state.config.fixedTickHz);
            limits.launchSpacingTicks = runtimeRuleFramesToTicks(2, state.config.fixedTickHz);
            limits.ingressTicks = runtimeRuleFramesToTicks(20, state.config.fixedTickHz);
            limits.egressTicks = runtimeRuleFramesToTicks(20, state.config.fixedTickHz);
            candidate.reset(limits);
        }
        result = candidate.requestStrike(request);
        if (result.accepted) {
            state.airstrikes = std::move(candidate);
            state.repaintRequested = true;
        }
    } else {
        result.veto = RuntimeAirstrikeVeto::InvalidCaller;
    }
    recordRuntimeProjectileReplayEvent(
        state,
        projectile,
        result.accepted
            ? "AirstrikeDesignatorAccepted"
            : "AirstrikeDesignatorRejected"
    );
    ++state.actionSummary.impactsThisTick;
    if (!result.accepted) completeRuntimeAirstrikeDesignatorBehavior(state,
        projectile.attackerObjectId, projectile.targetObjectId);
    state.simulationStatus = result.accepted
        ? "airstrike target marked"
        : std::string("airstrike designator vetoed: ")
            + runtimeAirstrikeVetoName(result.veto);
    return true;
}

bool authorizeRuntimeAirstrikeAttributedImpact(
    RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile) {
    const bool attributed = projectile.airstrikeStrikeId > 0
        && projectile.airstrikePlaneToken > 0;
    if (!attributed) {
        return projectile.airstrikeStrikeId == 0
            && projectile.airstrikePlaneToken == 0;
    }
    const RuntimeAirstrikeRootAuthority* liveAuthority =
        runtimeAirstrikeRootAuthorityForPair(
            state,
            projectile.airstrikeStrikeId,
            projectile.airstrikePlaneToken
        );
    if (liveAuthority == nullptr
        || !projectile.launchSourceValidated
        || projectile.launchSourceKind
            != static_cast<int>(WorldObjectKind::Unit)
        || !projectile.launchSourceAuthoredAircraft
        || toLowerAsciiLocal(trimAsciiLocal(projectile.owner))
            != toLowerAsciiLocal(trimAsciiLocal(liveAuthority->owner))
        || toLowerAsciiLocal(trimAsciiLocal(projectile.launchSourceType))
            != toLowerAsciiLocal(
                trimAsciiLocal(liveAuthority->aircraftTypeId)
            )) {
        return false;
    }

    const bool root = projectile.parentActorId == -1
        && projectile.generation == 0;
    if (!root) {
        return projectile.actorId != liveAuthority->rootProjectileActorId
            && projectile.parentActorId > 0
            && projectile.parentActorId < projectile.actorId
            && projectile.generation > 0
            && projectile.attackerObjectId == liveAuthority->planeObjectId;
    }
    const auto round = std::find_if(liveAuthority->rounds.begin(), liveAuthority->rounds.end(),
        [&](const auto& value) { return value.actorId == projectile.actorId; });
    if (round == liveAuthority->rounds.end() || round->terminalTick >= 0
        || projectile.attackerObjectId != liveAuthority->planeObjectId
        || projectile.targetObjectId != liveAuthority->targetObjectId
        || projectile.spawnTick != round->launchTick
        || toLowerAsciiLocal(trimAsciiLocal(projectile.weaponId))
            != toLowerAsciiLocal(trimAsciiLocal(liveAuthority->weaponId))
        || toLowerAsciiLocal(trimAsciiLocal(projectile.warheadId))
            != toLowerAsciiLocal(trimAsciiLocal(liveAuthority->warheadId))) return false;
    if (state.airstrikes.rootProjectileReceiptStatus(projectile.airstrikeStrikeId,
            projectile.airstrikePlaneToken) != RuntimeAirstrikeProjectileReceiptStatus::Pending) return false;
    auto& authority = state.airstrikeRootAuthoritiesByActor.at(liveAuthority->rootProjectileActorId);
    auto& mutableRound = authority.rounds[static_cast<std::size_t>(round - liveAuthority->rounds.begin())];
    mutableRound.terminalTick = state.tick;
    finishRuntimeAirstrikeSalvo(state, authority);
    return true;
}

std::string runtimeConventionalImpactAnimation(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WarheadType* warhead,
    const RuntimeCellPoint& impactCell) {
    if (warhead == nullptr || !warhead->conventional) {
        return projectile.impactAnimId;
    }
    const TerrainCell* terrain = findRuntimeTerrainCellAt(state, impactCell);
    if (terrain == nullptr) {
        return projectile.impactAnimId;
    }
    const RuntimeTerrainInfo terrainInfo =
        runtimeCachedTerrainInfoForCell(state, *terrain);
    if (!terrainInfo.water || terrainInfo.bridge) {
        return projectile.impactAnimId;
    }

    std::vector<std::string> splashes = splitRuntimeTokenList(
        state.rules.getAudioVisualValue("SplashList")
    );
    splashes.erase(
        std::remove_if(
            splashes.begin(),
            splashes.end(),
            [&state](const std::string& animationId) {
                return state.art.findArtType(animationId) == nullptr;
            }
        ),
        splashes.end()
    );
    if (splashes.empty()) {
        return projectile.impactAnimId;
    }
    std::uint64_t seed = runtimeStableHashString(
        "RuntimeConventionalSplash/1"
    );
    runtimeStableHashMix(seed, projectile.actorId);
    runtimeStableHashMix(seed, impactCell.x);
    runtimeStableHashMix(seed, impactCell.y);
    return splashes[static_cast<std::size_t>(
        seed % static_cast<std::uint64_t>(splashes.size())
    )];
}

bool applyRuntimeFireWarheadToIce(
    RuntimeProductionHostState& state,
    const WarheadType* warhead,
    const RuntimeCellPoint& impactCell) {
    if (warhead == nullptr || !warhead->fire) {
        return false;
    }
    for (TerrainCell& terrain : state.world.accessTerrainCells()) {
        if (terrain.x != impactCell.x
            || terrain.y != impactCell.y
            || terrain.iceGrowth <= 0) {
            continue;
        }
        terrain.iceGrowth = 0;
        state.navigationRefreshPending = true;
        state.pathTerrainCache.valid = false;
        state.pathTerrainCache.staticCells.clear();
        state.attackApproachCache.clear();
        state.radarTerrainKey.clear();
        state.repaintRequested = true;
        return true;
    }
    return false;
}

const WorldObjectInstance* runtimeTypeImmuneSource(
    const RuntimeGameSessionState& state, int objectId) {
    const auto* source = findRuntimeObjectByIdIncludingInactive(state, objectId);
    if (!source || source->health <= 0) return nullptr;
    if (source->alive || runtimeCombatSourceAvailable(state.rules,
            state.world.getObjects(), *source)) return source;
    if (source->garrisonHostObjectId >= 0) {
        const auto* host = findObjectById(state, source->garrisonHostObjectId);
        if (host && std::find(host->garrisonOccupantIds.begin(),
                host->garrisonOccupantIds.end(), objectId) != host->garrisonOccupantIds.end())
            return source;
    }
    if (source->mission == "ParasiteLimbo") {
        for (const auto& [hostId, link] : state.specialMechanisms.parasiteLinks())
            if (link.sourceObjectId == objectId && findObjectById(state, hostId)) return source;
    }
    if (state.specialMechanisms.isCarrierChildDocked(objectId)) return source;
    return nullptr;
}

std::string runtimeTypeImmuneHouse(const RuntimeGameSessionState& state, const std::string& owner) {
    std::string key = toLowerAsciiLocal(trimAsciiLocal(owner));
    if (state.campaignMode) {
        const auto alias = state.triggers.campaignCanonicalHouseByAlias.find(key);
        if (alias != state.triggers.campaignCanonicalHouseByAlias.end())
            key = toLowerAsciiLocal(trimAsciiLocal(alias->second));
    }
    return key;
}

bool runtimeDamageIsTypeImmune(const RuntimeGameSessionState& state,
    const WorldObjectInstance& target, const WorldObjectInstance* source,
    int authoredDamage, bool ignoreDefenses) {
    if (ignoreDefenses || authoredDamage < 0 || !source || source->kind != target.kind
        || runtimeTypeImmuneHouse(state, source->owner).empty()
        || runtimeTypeImmuneHouse(state, source->owner)
            != runtimeTypeImmuneHouse(state, target.owner)) return false;
    if (target.kind == WorldObjectKind::Structure) {
        const auto* type = findRuntimeBuildingTypeById(state.rules, target.type);
        return type && type->typeImmune
            && type == findRuntimeBuildingTypeById(state.rules, source->type);
    }
    const auto* type = findRuntimeUnitType(state.rules, target);
    return type && type->typeImmune && type == findRuntimeUnitType(state.rules, *source);
}

const WorldObjectInstance* runtimeFlakImpactBodyTarget(
    const RuntimeGameSessionState& state, const RuntimeProjectileEffect& projectile,
    const RuntimeProjectileTargetPosition& hit) {
    const auto* type = findRuntimeProjectileType(state.rules, projectile.projectileId);
    if (projectile.targetObjectId >= 0 || !type || !type->flakScatter
        || !projectile.aa || projectile.ag || projectile.damage <= 0
        || projectile.cellSpread <= 0.0 || projectile.particleDamagePulse) return nullptr;
    const auto* warhead = findRuntimeWarheadType(state.rules, projectile.warheadId);
    if (warhead && (warhead->ivanBomb || warhead->bombDisarm || warhead->psychedelic
        || warhead->makesDisguise || warhead->mindControl || warhead->electricAssault
        || warhead->temporal)) return nullptr;
    // Keep scattered, non-tracking physical endpoints. Only the saved visual
    // offset moves to a body in the existing damage footprint. A miss must
    // remain a miss, including after the original shooter disappears.
    WorldObjectInstance source;
    source.instanceId = projectile.attackerObjectId;
    source.owner = projectile.owner;
    const auto* attacker = findObjectById(state, projectile.attackerObjectId);
    if (attacker && attacker->alive) source.owner = attacker->owner;
    const auto* weapon = findRuntimeWeaponType(state.rules, projectile.weaponId);
    const bool lockedDamage = runtimeProjectileHasLockedDeathWeaponDamage(projectile);
    const int authoredDamage = weapon && !lockedDamage
        ? runtimeCombatProjectileAuthoredDamage(state, projectile, *weapon) : projectile.damage;
    const WorldObjectInstance* nearest = nullptr;
    double nearestDistance = std::numeric_limits<double>::infinity();
    for (const auto& candidate : state.world.getObjects()) {
        const double distance = runtimeCellDistanceToObjectFootprint(state, hit.cell, candidate);
        if (!candidate.alive || candidate.health <= 0 || !candidate.airborne
            || candidate.kind == WorldObjectKind::Structure || candidate.tankBunkerHostId >= 0
            || candidate.instanceId == projectile.attackerObjectId
            || distance > projectile.cellSpread
            || !runtimeProjectileCanHitTarget(state, projectile, candidate)
            || !runtimeWarheadCanAffectTarget(state, projectile, source, candidate)
            || runtimeObjectIgnoresDamage(state, candidate)
            || (warhead && warhead->tiberium && runtimeObjectIsTiberiumProof(state, candidate))
            || (state.specialMechanismsInitialized && state.specialMechanisms.isTemporalFrozen(candidate.instanceId))
            || (state.superWeaponsInitialized && state.superWeapons.isObjectProtected(candidate.instanceId, state.tick))
            || runtimeDamageIsTypeImmune(state, candidate,
                runtimeTypeImmuneSource(state, projectile.attackerObjectId), authoredDamage)) continue;
        const int damage = !weapon ? projectile.damage : runtimeAuthoredDamageAgainst(state,
            candidate, projectile.spawnedMissileDamage ? projectile.spawnedMissileBaseDamage : authoredDamage,
            lockedDamage ? projectile.warheadId : weapon->warhead, distance,
            lockedDamage ? 1.0 : projectile.veteranDamageMultiplier);
        if (damage <= 0) continue;
        const double dx = runtimeObjectRenderX(candidate) - hit.x;
        const double dy = runtimeObjectRenderY(candidate) - hit.y;
        const double bodyDistance = dx * dx + dy * dy;
        if (!std::isfinite(bodyDistance)) continue;
        if (bodyDistance < nearestDistance || (bodyDistance == nearestDistance
            && (!nearest || candidate.instanceId < nearest->instanceId))) {
            nearest = &candidate;
            nearestDistance = bodyDistance;
        }
    }
    return nearest;
}

bool applyRuntimeProjectileImpact(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    int attachedIvanBombTargetId) {
    if (state.runtimeProjectileImpactObserver) {
        state.runtimeProjectileImpactObserver(projectile, false);
    }
    if (toLowerAsciiLocal(trimAsciiLocal(projectile.renderIntent))
            == "prismsupportbeam" || projectile.renderIntent == "diskLaserBeam"
            || projectile.renderIntent == "magBeamVisual" || projectile.renderIntent == "sonicWave") {
        // These are visual links; their gameplay hit has already been handled.
        // The caller still expires the actor and records that lifecycle event,
        // but no impact, warhead, bridge, ore, or damage authority is created.
        return true;
    }
    ensureRuntimeSpecialMechanismsInitialized(state);
    ensureRuntimeSuperWeaponsInitialized(state);
    const WarheadType* exclusiveWarhead = findRuntimeWarheadType(
        state.rules,
        projectile.warheadId
    );
    if (exclusiveWarhead != nullptr && exclusiveWarhead->airstrike) {
        const WeaponType* exclusiveWeapon = findRuntimeWeaponType(
            state.rules,
            projectile.weaponId
        );
        if (exclusiveWeapon == nullptr) {
            recordRuntimeProjectileReplayEvent(
                state,
                projectile,
                "AirstrikeDesignatorRejected"
            );
            ++state.actionSummary.impactsThisTick;
            state.simulationStatus =
                "airstrike designator vetoed: missing exact weapon";
            return true;
        }
        return applyRuntimeAirstrikeDesignatorImpactExclusive(
            state,
            projectile,
            *exclusiveWeapon,
            *exclusiveWarhead
        );
    }
    if (projectile.airstrikeStrikeId != 0
        || projectile.airstrikePlaneToken != 0) {
        if (!authorizeRuntimeAirstrikeAttributedImpact(state, projectile)) {
            recordRuntimeProjectileReplayEvent(
                state,
                projectile,
                "AirstrikeProjectileRejected"
            );
            ++state.actionSummary.impactsThisTick;
            state.simulationStatus =
                "airstrike projectile authority rejected";
            return true;
        }
    }
    if (state.specialMechanisms.pendingParasiteLaunches().find(
            projectile.actorId
        ) != state.specialMechanisms.pendingParasiteLaunches().end()) {
        return applyRuntimeParasiteProjectileImpactExclusive(
            state,
            projectile
        );
    }
    const WarheadType* locomotorWarhead = findRuntimeWarheadType(
        state.rules,
        projectile.warheadId
    );
    if (locomotorWarhead != nullptr && locomotorWarhead->isLocomotor) {
        const WeaponType* locomotorWeapon = findRuntimeWeaponType(
            state.rules,
            projectile.weaponId
        );
        if (locomotorWeapon == nullptr) {
            recordRuntimeProjectileReplayEvent(
                state,
                projectile,
                "MagBeamImpactRejected"
            );
            ++state.actionSummary.impactsThisTick;
            state.simulationStatus =
                "mag beam impact vetoed: missing exact weapon";
            return true;
        }
        return applyRuntimeMagBeamProjectileImpactExclusive(
            state,
            projectile,
            *locomotorWeapon,
            *locomotorWarhead
        );
    }
    const WorldObjectInstance* currentTarget =
        findObjectById(state, projectile.targetObjectId);
    RuntimeProjectileTargetPosition impactPosition;
    if (currentTarget != nullptr && currentTarget->alive) {
        impactPosition = runtimeProjectileTargetPosition(
            state,
            *currentTarget
        );
    } else {
        impactPosition.cell = projectile.toCell;
        impactPosition.x = projectile.hasTargetPosition
            ? projectile.targetX
            : static_cast<double>(projectile.toCell.x);
        impactPosition.y = projectile.hasTargetPosition
            ? projectile.targetY
            : static_cast<double>(projectile.toCell.y);
        impactPosition.level = projectile.toLevel;
    }
    const RuntimeCellPoint impactCell = impactPosition.cell;
    const int impactLevel = impactPosition.level;
    const auto* impactBodyTarget = currentTarget ? currentTarget
        : runtimeFlakImpactBodyTarget(state, projectile, impactPosition);
    const RuntimeAirImpactBodyCenter impactBodyCenter = impactBodyTarget
        ? captureRuntimeAirImpactBodyCenter(state, *impactBodyTarget, impactPosition)
        : RuntimeAirImpactBodyCenter{};
    const WarheadType* projectileWarhead =
        findRuntimeWarheadType(state.rules, projectile.warheadId);
    const bool psychedelicImpact = projectileWarhead != nullptr
        && projectileWarhead->psychedelic;
    const bool timedBombRelationshipWarhead = projectileWarhead != nullptr
        && (projectileWarhead->ivanBomb
            || projectileWarhead->bombDisarm);
    RuntimeProjectileEffect relationshipImpact = projectile;
    if (timedBombRelationshipWarhead) {
        relationshipImpact.impactAnimId.clear();
        relationshipImpact.particleId.clear();
        relationshipImpact.damage = 0;
        relationshipImpact.cellSpread = 0.0;
        relationshipImpact.bright = false;
    } else {
        relationshipImpact.impactAnimId = runtimeConventionalImpactAnimation(
            state,
            projectile,
            projectileWarhead,
            impactCell
        );
    }
    if (!timedBombRelationshipWarhead
        && projectileWarhead != nullptr
        && !projectile.suppressWarheadParticleSpawn) {
        ensureRuntimeParticlesInitialized(state);
        RuntimeParticleSpawnContext particleContext;
        particleContext.sourceObjectId = projectile.attackerObjectId;
        particleContext.owner = projectile.owner;
        particleContext.sourceTag = "warhead:" + projectileWarhead->id;
        particleContext.position = {
            impactPosition.x * 256.0,
            impactPosition.y * 256.0,
            static_cast<double>(impactPosition.level) * 256.0
        };
        particleContext.direction = {
            (impactPosition.x - projectile.sourceX) * 256.0,
            (impactPosition.y - projectile.sourceY) * 256.0,
            (static_cast<double>(impactPosition.level) - projectile.sourceZ)
                * 256.0
        };
        particleContext.seed =
            (static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(projectile.actorId)
             ) << 32U)
            ^ static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(state.tick)
              )
            ^ 0x9e3779b97f4a7c15ULL;
        const RuntimeParticleSpawnResult particleSpawn =
            state.particles.spawnFromWarhead(
                state.rules,
                *projectileWarhead,
                particleContext
            );
        if (particleSpawn.spawned()) {
            relationshipImpact.particleId.clear();
        }
    }
    const RuntimeProjectileEffect& renderedImpact = relationshipImpact;
    if (!projectile.particleDamagePulse) {
        spawnRuntimeImpact(
            state,
            impactCell,
            impactLevel,
            false,
            renderedImpact.impactAnimId,
            projectile.warheadId,
            renderedImpact.damage,
            renderedImpact.cellSpread,
            renderedImpact.bright,
            renderedImpact.particleId,
            &renderedImpact,
            &impactPosition,
            &impactBodyCenter
        );
    }
    recordRuntimeProjectileReplayEvent(
        state,
        renderedImpact,
        "ProjectileImpact"
    );
    ++state.actionSummary.impactsThisTick;
    (void)applyRuntimeFireWarheadToIce(
        state,
        projectileWarhead,
        impactCell
    );
    const bool buildingDebris =
        toLowerAsciiLocal(projectile.renderIntent) == "buildingdebris";
    const bool crateObjectOnlyImpact =
        toLowerAsciiLocal(projectile.renderIntent) == "crateobjectonly";
    const bool sonicAmbientPulse = projectile.renderIntent == "sonicAmbientPulse";
    const bool suppressEnvironmentalDamage =
        buildingDebris || crateObjectOnlyImpact;
    const RuntimeOreDamageResult oreDamage =
        suppressEnvironmentalDamage || sonicAmbientPulse
            || timedBombRelationshipWarhead
            || psychedelicImpact
            ? RuntimeOreDamageResult{}
            : applyRuntimeTiberiumWarheadOreDamage(
                state,
                impactCell,
                projectile.damage,
                projectile.cellSpread,
                renderedImpact.impactAnimId,
                projectileWarhead
            );
    const WeaponType* directImpactWeapon = findRuntimeWeaponType(state.rules, projectile.weaponId);
    const bool bridgeHutDemolitionImpact = currentTarget != nullptr
        && runtimeObjectIsBridgeRepairHut(state, *currentTarget)
        && (projectile.renderIntent == "bridgeHutDemolitionExplosion"
            || (!timedBombRelationshipWarhead && directImpactWeapon != nullptr
                && (directImpactWeapon->suicide || directImpactWeapon->sabotageCursor)));
    const RuntimeBridgeDamageResult bridgeDamage = bridgeHutDemolitionImpact
        ? demolishRuntimeBridgeByHut(state, *currentTarget, projectile.attackerObjectId,
            projectile.owner, runtimeDeathWeaponCauseForProjectile(projectile))
        :
        !suppressEnvironmentalDamage && !sonicAmbientPulse && !timedBombRelationshipWarhead
            && !psychedelicImpact && projectile.ag
            ? damageRuntimeBridgeAtCell(
                state,
                impactCell,
                projectile.damage,
                static_cast<int>(std::ceil(
                    std::max(0.0, projectile.cellSpread)
                )),
                projectile.attackerObjectId,
                projectile.owner,
                runtimeDeathWeaponCauseForProjectile(projectile)
            )
            : RuntimeBridgeDamageResult{};
    if ((state.campaignMode || state.cooperativeSession.active()) && bridgeDamage.destroyed) {
        for (int cell : bridgeDamage.destroyedCellKeys) {
            state.triggers.campaignBridgeDestroyedTickByCell[cell] = state.tick;
        }
    }

    WorldObjectInstance* attacker =
        findMutableObjectById(state, projectile.attackerObjectId);
    WorldObjectInstance orphanAttacker;
    if (attacker == nullptr || !attacker->alive) {
        orphanAttacker.instanceId = projectile.attackerObjectId;
        orphanAttacker.owner = projectile.owner;
        orphanAttacker.alive = true;
        attacker = &orphanAttacker;
    }

    const WeaponType* weapon =
        findRuntimeWeaponType(state.rules, projectile.weaponId);
    const RuntimeTerrainProjectileDamageResult terrainDamage =
        suppressEnvironmentalDamage
            || timedBombRelationshipWarhead
            || psychedelicImpact
            ? RuntimeTerrainProjectileDamageResult{}
            : applyRuntimeProjectileTerrainDamage(
                state,
                projectile,
                impactCell,
                sonicAmbientPulse ? nullptr : weapon,
                projectileWarhead
            );
    const RuntimeTerrainProjectileDamageResult overlayWallDamage =
        suppressEnvironmentalDamage || timedBombRelationshipWarhead || psychedelicImpact
            ? RuntimeTerrainProjectileDamageResult{}
            : applyRuntimeProjectileOverlayWallDamage(state, projectile, impactCell,
                sonicAmbientPulse ? nullptr : weapon, projectileWarhead);
    if (!psychedelicImpact
        && !timedBombRelationshipWarhead
        && projectileWarhead != nullptr
        && weapon != nullptr && weapon->radLevel > 0) {
        // Radiation marks damage/immunity; only an authored RadLevel creates
        // a site. The ordinary Desolator beam deliberately has no RadLevel.
        const SpecialMechanismRules& specialRules =
            state.specialMechanisms.rules();
        const int maximumStrength =
            std::max(1, specialRules.radiationLevelMax);
        const int strength = std::clamp(
            weapon->radLevel,
            1,
            maximumStrength
        );
        // CellInset controls auto-deployment, not the radiation radius.
        const int radius = static_cast<int>(std::max(0.0, projectile.cellSpread));
        state.specialMechanisms.spawnRadiationArea(
            projectile.attackerObjectId,
            projectile.owner,
            {impactCell.x, impactCell.y},
            radius,
            strength,
            std::max(1, projectile.damage),
            specialRules.radiationSiteWarhead,
            state.tick,
            true,
            runtimeSpecialEventSink(state)
        );
    }
    if (!psychedelicImpact
        && !timedBombRelationshipWarhead
        && (projectile.electricBolt
        || (projectileWarhead != nullptr
            && projectileWarhead->electricAssault))) {
        const std::string color = projectile.electricColor.empty()
            ? (projectile.alternateElectricColor ? "255,200,255" : "80,80,255")
            : projectile.electricColor;
        // Visual attachment stays separate from the saved authoritative source.
        state.specialMechanisms.addElectricArc(
            projectile.attackerObjectId,
            projectile.targetObjectId,
            {
                projectile.sourceX,
                projectile.sourceY,
                projectile.sourceZ
            },
            {
                impactPosition.x,
                impactPosition.y,
                static_cast<double>(impactPosition.level)
            },
            color,
            projectile.alternateElectricColor,
            projectileWarhead != nullptr && projectileWarhead->electricAssault,
            state.tick,
            std::max(1, state.config.fixedTickHz / 6),
            runtimeSpecialEventSink(state),
            projectile.sourceScreenOffsetX, projectile.sourceScreenOffsetY,
            projectile.sourceNativeFixedX, projectile.sourceNativeFixedY
        );
    }

    const double radius = std::max(0.0, projectile.cellSpread);
    int damagedObjects = 0;
    int totalDamage = 0;
    int destroyedObjects = 0;
    std::vector<int> destroyedIds;
    bool destroyedStructure = false;
    bool playerObjectLost = false;
    bool playerBaseUnderAttack = false;
    bool playerOreMinerUnderAttack = false;
    std::vector<RuntimeVeterancyKillCredit> veterancyKillCredits;
    std::vector<int> deferredMindControlTargetIds;

    const auto* bunkerTarget = findObjectById(state, projectile.targetObjectId);
    const int protectedHostId = bunkerTarget == nullptr ? -1 : bunkerTarget->tankBunkerHostId;
    const bool penetratesBunker = projectileWarhead != nullptr && projectileWarhead->penetratesBunker;
    for (WorldObjectInstance& candidate : state.world.accessObjects()) {
        if (candidate.tankBunkerHostId >= 0 && !penetratesBunker) continue;
        if (!candidate.alive
            || candidate.instanceId == projectile.attackerObjectId) {
            continue;
        }

        if (projectile.renderIntent == "sonicAmbientPulse") {
            const auto terrain=state.terrainByCell.find(runtimeCellKey(impactCell.x,impactCell.y));
            if (terrain!=state.terrainByCell.end() && terrain->second
                && runtimeOverlayProvidesBridgeDeck(*terrain->second)
                && runtimeOverlayLooksHighBridge(*terrain->second)) {
                const bool upper=projectile.sourceZ>=runtimeEffectiveTerrainLevel(*terrain->second);
                if (candidate.onBridge!=upper) continue;
            }
        }

        const bool zeroRadiusPositionedImpact = radius <= 0.0
            && (projectile.particleDamagePulse
                || projectile.targetObjectId < 0);
        const double distance = projectile.particleDamagePulse
            && projectile.hasTargetPosition
            ? runtimeWorldDistanceToObjectFootprint(
                state,
                projectile.targetX,
                projectile.targetY,
                candidate
            )
            : runtimeCellDistanceToObjectFootprint(
                state,
                impactCell,
                candidate
            );
        const bool zeroRadiusPositionedCellHit =
            zeroRadiusPositionedImpact
            && runtimeCellDistanceToObjectFootprint(
                state,
                impactCell,
                candidate
            ) <= 0.0;
        const bool bunkerIntercept = !penetratesBunker && candidate.instanceId == protectedHostId;
        if (candidate.instanceId != projectile.targetObjectId && !bunkerIntercept
            && (!zeroRadiusPositionedCellHit
                && (radius <= 0.0 || distance > radius))) {
            continue;
        }

        if (!runtimeProjectileCanHitTarget(state, projectile, candidate)
            || !runtimeWarheadCanAffectTarget(
                state,
                projectile,
                *attacker,
                candidate
            )) {
            continue;
        }
        if (!timedBombRelationshipWarhead
            && projectileWarhead != nullptr
            && projectileWarhead->tiberium
            && runtimeObjectIsTiberiumProof(state, candidate)) {
            continue;
        }
        // Iron Curtain and Force Shield protection must participate in the
        // Initialize new spawns before their first impact, including scripted
        // objects delivered after the fixed-tick extension update.
        runtimeInitializeModExtensions(candidate, state.rules, state.config.fixedTickHz);
        const bool extensionProtected = state.superWeaponsInitialized
            && state.superWeapons.isObjectProtected(candidate.instanceId, state.tick);
        if (projectileWarhead && !runtimeObjectIgnoresDamage(state, candidate))
            runtimeAttachEffect(candidate, state.rules, projectileWarhead->id, true,
                state.config.fixedTickHz, extensionProtected);
        // Stasis blocks ordinary damage, not the continuing erasure beam.
        if (state.specialMechanisms.isTemporalFrozen(candidate.instanceId)
            && (projectileWarhead == nullptr || !projectileWarhead->temporal)) continue;
        // ordinary projectile path too, not only SuperWeaponSystem's own
        // radial-damage helper.  This also blocks hostile special warheads
        // such as mind control and temporal displacement while protected.
        if (state.superWeaponsInitialized
            && state.superWeapons.isObjectProtected(
                candidate.instanceId,
                state.tick
            )) {
            continue;
        }
        const bool parasiteHealingEvent = weapon != nullptr
            ? weapon->damage < 0
            : projectile.damage < 0;
        const bool parasiteSonicEvent = weapon != nullptr && weapon->sonic;
        if ((parasiteHealingEvent || parasiteSonicEvent)
            && state.specialMechanisms.parasiteLinks().find(
                candidate.instanceId
            ) != state.specialMechanisms.parasiteLinks().end()
            && !handleRuntimeParasiteHostEvent(
                state,
                candidate.instanceId,
                parasiteHealingEvent
                    ? ParasiteHostEventKind::Healed
                    : ParasiteHostEventKind::SonicHit,
                std::max(1, state.config.fixedTickHz)
            )) {
            continue;
        }
        if (psychedelicImpact) {
            const RuntimePsychicStatusHitResult psychicResult =
                applyRuntimePsychicProjectileHit(
                    state,
                    projectile,
                    candidate,
                    *projectileWarhead
                );
            if (psychicResult.accepted) {
                ++damagedObjects;
            }
            // Psychedelic is an exclusive status warhead. It never enters
            // relationship warheads, hit-point damage, death, or shrapnel
            // side effects for this candidate.
            continue;
        }
        if (projectileWarhead != nullptr
            && projectileWarhead->bombDisarm) {
            if (candidate.instanceId == projectile.targetObjectId) {
                std::string specialReason;
                const bool disarmed =
                    state.specialMechanisms.tryDisarmTimedBomb(
                        state.world.getObjects(),
                        state.rules,
                        projectile.attackerObjectId,
                        candidate.instanceId,
                        projectile.warheadId,
                        state.tick,
                        runtimeSpecialEventSink(state),
                        specialReason
                    );
                if (disarmed) {
                    ++damagedObjects;
                }
                const auto behavior = state.behaviors.find(
                    projectile.attackerObjectId
                );
                if (behavior != state.behaviors.end()
                    && behavior->second.kind == RuntimeBehaviorKind::Attack
                    && behavior->second.targetObjectId
                        == projectile.targetObjectId) {
                    behavior->second.state = RuntimeBehaviorState::Complete;
                    behavior->second.weaponCooldownTicks = 0;
                }
            }
            continue;
        }
        if (projectileWarhead != nullptr && projectileWarhead->ivanBomb) {
            if (candidate.instanceId == projectile.targetObjectId) {
                if (!runtimeBuildingBombOrderRelationLegal(
                        state, *attacker, candidate, projectile.affectsAllies)) continue;
                if (!runtimeBridgeHutDemolitionCommandLegal(state, candidate,
                        weapon, projectileWarhead)) continue;
                std::string specialReason;
                const bool attached =
                    state.specialMechanisms.tryAttachIvanBomb(
                        state.world.getObjects(),
                        state.rules,
                        projectile.attackerObjectId,
                        candidate.instanceId,
                        state.tick,
                        state.config.fixedTickHz,
                        runtimeSpecialEventSink(state),
                        specialReason
                    );
                if (attached) {
                    cancelRuntimeTimedBombIntentsForTarget(
                        state,
                        candidate.instanceId,
                        -1,
                        "ivanBombTargetAttached"
                    );
                    ++damagedObjects;
                }
                const auto behavior = state.behaviors.find(
                    projectile.attackerObjectId
                );
                if (behavior != state.behaviors.end()
                    && behavior->second.kind == RuntimeBehaviorKind::Attack
                    && behavior->second.targetObjectId
                        == projectile.targetObjectId) {
                    behavior->second.state = RuntimeBehaviorState::Complete;
                    behavior->second.weaponCooldownTicks = 0;
                }
            }
            continue;
        }
        if (projectileWarhead != nullptr
            && projectileWarhead->makesDisguise) {
            if (candidate.instanceId == projectile.targetObjectId) {
                ensureRuntimeSpiesInitialized(state);
                const RuntimeSpyDisguiseResult disguise =
                    state.spies.applyMakesDisguise(
                        makeRuntimeSpyContext(state),
                        projectile.attackerObjectId,
                        candidate.instanceId,
                        projectile.warheadId,
                        state.tick
                    );
                (void)applyRuntimeSpyActions(state, disguise.actions);
                const auto sourceBehavior = state.behaviors.find(
                    projectile.attackerObjectId
                );
                if (sourceBehavior != state.behaviors.end()
                    && sourceBehavior->second.kind
                        == RuntimeBehaviorKind::Attack
                    && sourceBehavior->second.targetObjectId
                        == projectile.targetObjectId) {
                    sourceBehavior->second.state =
                        RuntimeBehaviorState::Complete;
                    sourceBehavior->second.weaponCooldownTicks = 0;
                }
                if (disguise.accepted) {
                    recordRuntimeTeamTargetInteraction(state, projectile.attackerObjectId,
                        projectile.targetObjectId);
                    ++damagedObjects;
                }
            }
            // MakesDisguise is an identity relationship, never hit-point
            // damage, even when a mod gives its weapon a non-zero Damage.
            continue;
        }
        if ((state.campaignMode || state.cooperativeSession.active())
            && !suppressEnvironmentalDamage
            && !trimAsciiLocal(projectile.owner).empty()
            && (projectile.damage > 0
                || (projectileWarhead != nullptr
                    && (projectileWarhead->mindControl
                        || projectileWarhead->electricAssault
                        || projectileWarhead->temporal)))) {
            state.triggers.campaignAttackedTickByObject[
                candidate.instanceId
            ] = state.tick;
            state.triggers.campaignAttackerHouseByObject[
                candidate.instanceId
            ] = projectile.owner;
        }
        if (projectileWarhead != nullptr && projectileWarhead->mindControl) {
            if (runtimeObjectManagedByAirstrike(
                    state,
                    candidate.instanceId
                )) {
                state.simulationStatus =
                    "mind control vetoed: Airstrike owns plane";
                continue;
            }
            // Owner transitions swap the detached world at commit.  Defer
            // this relationship until after the current world iteration so
            // no iterator or object reference can survive that swap.
            deferredMindControlTargetIds.push_back(candidate.instanceId);
            continue;
        }
        if (projectileWarhead != nullptr
            && projectileWarhead->electricAssault) {
            std::string chargeReason;
            const bool charged = state.specialMechanisms.applyElectricCharge(
                state.world.accessObjects(),
                state.rules,
                projectile.attackerObjectId,
                candidate.instanceId,
                state.tick,
                weapon == nullptr
                    ? std::max(1, state.config.fixedTickHz * 2)
                    : runtimeWeaponCooldownTicks(*weapon, state.config.fixedTickHz),
                1,
                [ &state ](const std::string& left, const std::string& right) {
                    return runtimeOwnersAreAllied(state, left, right);
                },
                runtimeSpecialEventSink(state),
                chargeReason
            );
            if (charged) {
                ++damagedObjects;
            }
            // ElectricAssault is a relationship/overpowering warhead.  Its
            // authored verses describe the eligible building class, but it
            // must never fall through to ordinary hit-point damage.
            continue;
        }
        if (projectileWarhead != nullptr && projectileWarhead->temporal) {
            if (runtimeObjectManagedByAirstrike(
                    state,
                    candidate.instanceId
                )) {
                state.simulationStatus =
                    "temporal impact vetoed: Airstrike owns plane";
                continue;
            }
            if (runtimeObjectSuspendedByMagBeam(
                    state,
                    candidate.instanceId
                )) {
                state.simulationStatus =
                    "temporal impact vetoed: mag beam owns locomotor";
                continue;
            }
            bool completed = false;
            std::string specialReason;
            // Temporal::Start uses authored Type.Strength * 10, independently
            // of current health and difficulty. The hit attaches the ray;
            // continuous updates supply Damage per rule frame, not per ROF.
            const int requiredProgress = static_cast<int>(std::clamp<std::int64_t>(
                static_cast<std::int64_t>(runtimeCachedCapabilityProfileForObject(
                    state, candidate).ui.maxStrength) * 10, 1, std::numeric_limits<int>::max()));
            const bool applied = state.specialMechanisms.applyTemporalHit(
                state.world.accessObjects(),
                state.rules,
                projectile.attackerObjectId,
                candidate.instanceId,
                0,
                requiredProgress,
                state.tick,
                [ &state ](const std::string& left, const std::string& right) {
                    return runtimeOwnersAreAllied(state, left, right);
                },
                runtimeSpecialEventSink(state),
                completed,
                specialReason,
                [&] {
                    const auto order = state.behaviors.find(projectile.attackerObjectId);
                    return order != state.behaviors.end()
                        && order->second.kind == RuntimeBehaviorKind::Attack
                        && order->second.targetObjectId == candidate.instanceId && order->second.forceFire;
                }()
            );
            if (applied) {
                ++damagedObjects;
            }
            continue;
        }
        // An attached Ivan charge damages its own building, including walls
        // and permanently immune huts. Splash and temporary protection keep
        // their ordinary policy; this permission exists only on this call.
        const bool attachedIvanBuildingBomb = attachedIvanBombTargetId >= 0
            && candidate.instanceId == attachedIvanBombTargetId
            && candidate.kind == WorldObjectKind::Structure;
        if (!attachedIvanBuildingBomb && runtimeObjectIgnoresDamage(state, candidate)) {
            // Immunity rejects this hit; it must not heal earlier Ivan damage.
            continue;
        }

        const int typeImmunityDamage = weapon && !projectile.particleDamagePulse
            && !runtimeProjectileHasLockedDeathWeaponDamage(projectile)
            ? runtimeCombatProjectileAuthoredDamage(state, projectile, *weapon) : projectile.damage;
        if (runtimeDamageIsTypeImmune(state, candidate,
                runtimeTypeImmuneSource(state, projectile.attackerObjectId), typeImmunityDamage)) continue;

        const auto damageAgainst = [&](const WorldObjectInstance& victim) {
        int damage = projectile.particleDamagePulse
            ? runtimeAuthoredDamageAgainst(
                state,
                victim,
                projectile.damage,
                projectile.warheadId,
                zeroRadiusPositionedCellHit ? 0.0 : distance,
                1.0
            )
            : runtimeProjectileHasLockedDeathWeaponDamage(projectile)
            ? runtimeAuthoredDamageAgainst(
                state,
                victim,
                projectile.damage,
                projectile.warheadId,
                distance,
                1.0
            )
            : projectile.spawnedMissileDamage
            ? runtimeAuthoredDamageAgainst(
                state,
                victim,
                projectile.spawnedMissileBaseDamage,
                projectile.warheadId,
                distance,
                projectile.veteranDamageMultiplier,
                attachedIvanBombTargetId >= 0
                    && victim.instanceId == attachedIvanBombTargetId
                    && victim.kind == WorldObjectKind::Structure
            )
            : (weapon == nullptr
                ? projectile.damage
                : runtimeAuthoredDamageAgainst(
                state,
                victim,
                runtimeCombatProjectileAuthoredDamage(state, projectile, *weapon),
                weapon->warhead,
                distance,
                projectile.veteranDamageMultiplier
            ));
        if (weapon == nullptr
            && !projectile.particleDamagePulse
            && !projectile.spawnedMissileDamage
            && damage > 0) {
            const double armorMultiplier = std::max(
                0.01,
                runtimeVeterancyAbilityMultiplier(
                    state,
                    victim,
                    VeteranAbility::Stronger
                )
            );
            damage = std::max(
                0,
                static_cast<int>(std::lround(
                    static_cast<double>(damage) / armorMultiplier
                ))
            );
        }
            return damage;
        };
        int damage = damageAgainst(candidate);
        if (runtimeActiveShield(candidate, state.rules, projectileWarhead)) {
            WorldObjectInstance unshielded = candidate;
            unshielded.modExtensions.shieldOnline = false;
            const int bodyDamage = damageAgainst(unshielded);
            damage = runtimeAbsorbShieldDamage(candidate, state.rules, projectileWarhead,
                damage, bodyDamage, state.config.fixedTickHz);
        } else if (damage > 0 && candidate.modExtensions.shieldHealth == 0
                && candidate.modExtensions.shieldCapacity > 0) {
            const auto* shield = state.rules.findShieldType(candidate.modExtensions.shieldType);
            if (shield && shield->respawnRestartInCombat)
                candidate.modExtensions.shieldTimer = shield->respawnRestartDelay > 0
                    ? runtimeModFrames(shield->respawnRestartDelay, state.config.fixedTickHz)
                    : runtimeShieldInterval(*shield, true, state.config.fixedTickHz);
        }
        if (candidate.health < 0) {
            candidate.health = 256;
        }
        const int healthBeforeDamage = candidate.health;
        const int maxHealth =
            std::max(1, runtimeMaxHealthForObject(state, candidate));
        candidate.health = std::clamp(
            static_cast<int>(std::clamp(static_cast<long long>(candidate.health) - damage, 0LL, static_cast<long long>(maxHealth))),
            0,
            maxHealth
        );
        (void)spawnRuntimeSparkyBuildingEffect(
            state,
            projectile,
            projectileWarhead,
            candidate,
            healthBeforeDamage,
            maxHealth,
            damage
        );
        const UnitType* proneUnit = candidate.kind == WorldObjectKind::Infantry
            ? findRuntimeUnitType(state.rules, candidate)
            : nullptr;
        const auto proneBehavior = state.behaviors.find(candidate.instanceId);
        const bool activelyMoving = proneBehavior != state.behaviors.end()
            && (proneBehavior->second.kind == RuntimeBehaviorKind::Move
                || (proneBehavior->second.kind == RuntimeBehaviorKind::Attack
                    && (proneBehavior->second.state
                            == RuntimeBehaviorState::Chasing
                        || !proneBehavior->second.path.empty()
                        || proneBehavior->second.moveCooldownTicks > 0)));
        if (damage > 0
            && candidate.health > 0
            && candidate.kind == WorldObjectKind::Infantry
            && (candidate.prone || !activelyMoving)
            && (proneUnit == nullptr || !proneUnit->fearless)
            && !runtimeVeterancyHasAbility(
                state,
                candidate,
                VeteranAbility::Fearless
            )
            && !runtimeObjectHasDeployedState(state, candidate)) {
            candidate.prone = true;
            candidate.proneTicksRemaining = std::max(
                1,
                state.config.fixedTickHz
            );
        }
        if (candidate.health < healthBeforeDamage) {
            runtimeRevealObjectAfterDamage(state, candidate, "damage");
            noteRuntimeTeamMemberDamaged(state, candidate.instanceId, projectile.attackerObjectId);
            (void)beginRuntimePanic(state, candidate, false, projectile.fromCell.x, projectile.fromCell.y);
        }
        ++damagedObjects;
        totalDamage += damage;

        const bool localPlayerObject =
            candidate.owner == state.economy.owner;
        if (damage > 0
            && localPlayerObject
            && candidate.kind == WorldObjectKind::Structure) {
            playerBaseUnderAttack = true;
            state.playerLastRadarCell = RuntimeMultiplayerGameplayCell{candidate.x, candidate.y};
        }
        if (damage > 0
            && localPlayerObject
            && runtimeObjectIsHarvester(state, candidate)) {
            playerOreMinerUnderAttack = true;
            state.playerLastRadarCell = RuntimeMultiplayerGameplayCell{candidate.x, candidate.y};
        }

        if (candidate.kind == WorldObjectKind::Structure
            && candidate.health > 0
            && healthBeforeDamage > maxHealth / 2
            && candidate.health <= maxHealth / 2) {
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, candidate.type);
            const std::string damageSound =
                building == nullptr ? std::string() : building->damageSound;
            emitRuntimeSound(
                state,
                trimAsciiLocal(damageSound).empty()
                    ? "BuildingDamaged"
                    : "sound:" + trimAsciiLocal(damageSound),
                runtimeSpatialAudioPositionForObject(candidate), true
            );
        }

        if (damage > 0
            && localPlayerObject
            && candidate.health > 0
            && candidate.kind != WorldObjectKind::Structure) {
            const UnitType* unit =
                findRuntimeUnitTypeById(state.rules, candidate.type);
            const int feedbackReadyTick =
                state.runtimeUnitFeedbackCooldownUntilTick[
                    candidate.instanceId
                ];
            if (unit != nullptr && state.tick >= feedbackReadyTick) {
                const std::optional<std::string> feedbackVoice =
                    runtimeVoiceEventFromField(
                        state,
                        candidate,
                        "feedback",
                        runtimeDamageFeedbackVoiceField(*unit)
                    );
                if (feedbackVoice.has_value()) {
                    emitRuntimeSound(state, *feedbackVoice, runtimeSpatialAudioPositionForObject(candidate));
                    state.runtimeUnitFeedbackCooldownUntilTick[
                        candidate.instanceId
                    ] = state.tick
                        + std::max(1, state.config.fixedTickHz * 3);
                }
            }
        }

        if (candidate.health <= 0) {
            if (damage > 0 && healthBeforeDamage > 0) {
                veterancyKillCredits.push_back(RuntimeVeterancyKillCredit{
                    candidate.instanceId,
                    candidate.owner,
                    runtimeVeterancyObjectCost(state, candidate),
                    candidate.veterancy,
                    runtimeVeterancyObjectAwardsExperience(state, candidate)
                });
            }
            playerObjectLost =
                playerObjectLost || (candidate.kind != WorldObjectKind::Structure && candidate.owner == state.economy.owner);
            destroyedIds.push_back(candidate.instanceId);
            destroyedStructure =
                destroyedStructure
                || candidate.kind == WorldObjectKind::Structure;
            const RuntimeProjectileTargetPosition deathImpactPosition =
                runtimeProjectileTargetPosition(state, candidate);
            const BuildingType* destroyedBuilding =
                candidate.kind == WorldObjectKind::Structure
                    ? findRuntimeBuildingTypeById(
                        state.rules,
                        candidate.type
                    )
                    : nullptr;
            const UnitType* destroyedUnit =
                candidate.kind == WorldObjectKind::Structure
                    ? nullptr
                    : findRuntimeUnitTypeById(state.rules, candidate.type);
            if (destroyedBuilding != nullptr) {
                spawnRuntimeBuildingDestructionEffects(
                    state,
                    candidate,
                    *destroyedBuilding,
                    projectile,
                    deathImpactPosition,
                    damage
                );
            } else if (destroyedUnit != nullptr) {
                spawnRuntimeUnitDestructionEffects(
                    state,
                    candidate,
                    *destroyedUnit,
                    projectile,
                    deathImpactPosition,
                    damage
                );
            } else {
                spawnRuntimeImpact(
                    state,
                    deathImpactPosition.cell,
                    deathImpactPosition.level,
                    true,
                    projectile.impactAnimId,
                    projectile.warheadId,
                    damage,
                    projectile.cellSpread,
                    true,
                    projectile.particleId,
                    &projectile,
                    &deathImpactPosition,
                    candidate.instanceId == projectile.targetObjectId ? &impactBodyCenter : nullptr
                );
            }
            std::string deathSound;
            if (candidate.kind == WorldObjectKind::Structure) {
                if (const BuildingType* building =
                        findRuntimeBuildingTypeById(
                            state.rules,
                            candidate.type
                        )) {
                    deathSound = building->dieSound;
                }
                emitRuntimeSound(
                    state,
                    trimAsciiLocal(deathSound).empty()
                        ? "BuildingDestroyed"
                        : "sound:" + trimAsciiLocal(deathSound),
                    runtimeSpatialAudioPositionForObject(candidate), trimAsciiLocal(deathSound).empty()
                );
            } else {
                emitRuntimeUnitDeathSound(state, candidate);
            }
            RuntimeObjectExitRequest objectExit;
            objectExit.objectId = candidate.instanceId;
            objectExit.kind = RuntimeObjectExitKind::Destroyed;
            objectExit.tick = state.tick;
            objectExit.mission = "Destroyed";
            objectExit.reason = "projectile lethal impact";
            objectExit.destructionEffectsSpawned = true;
            objectExit.unitDeathSoundHandled = true;
            objectExit.sourceObjectId = projectile.attackerObjectId;
            objectExit.sourceOwner = projectile.owner;
            objectExit.deathWeaponCause =
                runtimeDeathWeaponCauseForProjectile(projectile);
            if (queueRuntimeObjectExit(state, std::move(objectExit))
                    == RuntimeObjectExitQueueResult::Queued) {
                ++destroyedObjects;
            }
        }
    }

    for (int targetObjectId : deferredMindControlTargetIds) {
        const WorldObjectInstance* controller =
            findRuntimeObjectByIdIncludingInactive(
                state,
                projectile.attackerObjectId
            );
        const WorldObjectInstance* target =
            findRuntimeObjectByIdIncludingInactive(state, targetObjectId);
        RuntimeOwnerTransitionBatchResult transition;
        if (controller != nullptr
            && runtimeCombatSourceAvailable(state.rules, state.world.getObjects(), *controller)
            && controller->health > 0
            && target != nullptr
            && target->alive
            && target->health > 0) {
            RuntimeMindControlOwnerPreparation preparation;
            preparation.controllerObjectId = controller->instanceId;
            preparation.targetObjectId = targetObjectId;
            preparation.capacity =
                runtimeProjectileHasLockedDeathWeaponDamage(projectile)
                ? std::max(1, projectile.damage)
                : (weapon == nullptr
                    ? std::max(1, projectile.damage)
                    : std::max(1, weapon->damage));
            preparation.permanent = weapon != nullptr
                && weapon->permanentMindControl;
            preparation.infinite = weapon != nullptr
                && weapon->infiniteMindControl;
            preparation.allowStructures =
                projectileWarhead->profile.damagesBuildings;
            transition = applyRuntimeOwnerTransitionBatch(
                state,
                {{targetObjectId, target->owner, controller->owner}},
                RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits,
                "mindControlOwnerChanged",
                &preparation
            );
        } else {
            transition.error = "mind-control object unavailable";
        }
        if (transition.applied) {
            ++damagedObjects;
            continue;
        }

        const WorldObjectInstance* rejectedTarget =
            findRuntimeObjectByIdIncludingInactive(state, targetObjectId);
        runtimeSpecialEventSink(state)({
            "MindControlRejected",
            state.tick,
            projectile.attackerObjectId,
            targetObjectId,
            0,
            rejectedTarget == nullptr
                ? SpecialCell{}
                : SpecialCell{rejectedTarget->x, rejectedTarget->y},
            transition.error.empty()
                ? std::string("owner transition rejected")
                : transition.error
        });
        state.simulationStatus = "mind control rejected: "
            + (transition.error.empty()
                ? std::string("owner transition rejected")
                : transition.error);
    }

    (void)drainRuntimeObjectExitQueue(state);

    for (const RuntimeVeterancyKillCredit& credit : veterancyKillCredits) {
        (void)awardRuntimeVeterancyExperience(
            state,
            projectile.attackerObjectId,
            projectile.owner,
            credit
        );
    }

    // Temporal removal is deliberately finalized through the normal object
    // death path after the projectile loop. The temporal state machine has
    // already decided the target is gone; this keeps destruction effects,
    // transport cleanup, selection cleanup, and replay-visible ownership
    // cleanup identical to an ordinary lethal hit.
    for (WorldObjectInstance& candidate : state.world.accessObjects()) {
        if (!candidate.alive || candidate.mission != "TemporalRemoved") {
            continue;
        }
        playerObjectLost =
            playerObjectLost || (candidate.kind != WorldObjectKind::Structure && candidate.owner == state.economy.owner);
        destroyedIds.push_back(candidate.instanceId);
        destroyedStructure =
            destroyedStructure || candidate.kind == WorldObjectKind::Structure;
        // TemporalCompleted already spawned the authored WarpAway animation.
        // Erasure must not also emit ordinary explosions, fire or debris.
        RuntimeObjectExitRequest objectExit;
        objectExit.objectId = candidate.instanceId;
        objectExit.kind = RuntimeObjectExitKind::TemporalDestroyed;
        objectExit.tick = state.tick;
        objectExit.mission = "TemporalRemoved";
        objectExit.reason = "temporal removed";
        objectExit.sourceObjectId = projectile.attackerObjectId;
        objectExit.sourceOwner = projectile.owner;
        objectExit.deathWeaponCause =
            runtimeDeathWeaponCauseForProjectile(projectile);
        objectExit.suppressDeathWeapon = true;
        if (queueRuntimeObjectExit(state, std::move(objectExit))
                == RuntimeObjectExitQueueResult::Queued) {
            ++destroyedObjects;
        }
    }

    (void)drainRuntimeObjectExitQueue(state);

    if (!destroyedIds.empty()) {
        for (int destroyedId : destroyedIds) {
            releaseRuntimeSlaveMinerWorkersForRefinery(
                state,
                destroyedId,
                "refinery destroyed"
            );
        }
        if (destroyedStructure) {
            refreshRuntimeWallConnectionMasks(state);
        }
        state.attackApproachCache.clear();
        invalidateRuntimeTickOccupancyCache(state);
        invalidateRuntimeObjectProjectionCache(state);
        state.navigationRefreshPending = true;
        state.powerRefreshPending =
            state.powerRefreshPending || destroyedStructure;
    }
    const int baseWarningCooldown =
        std::max(1, state.config.fixedTickHz * 12);
    if (playerBaseUnderAttack
        && state.tick - state.runtimeLastBaseUnderAttackSoundTick
            >= baseWarningCooldown) {
        emitRuntimeSound(state, "BaseUnderAttackSiren");
        emitRuntimeSound(state, runtimeSoundIntentBaseUnderAttack);
        state.runtimeLastBaseUnderAttackSoundTick = state.tick;
    }
    if (playerOreMinerUnderAttack
        && state.tick - state.runtimeLastOreMinerUnderAttackSoundTick
            >= baseWarningCooldown) {
        emitRuntimeSound(state, runtimeSoundIntentOreMinerUnderAttack);
        state.runtimeLastOreMinerUnderAttackSoundTick = state.tick;
    }
    const int lossWarningCooldown =
        std::max(1, state.config.fixedTickHz * 2);
    if (playerObjectLost
        && state.tick - state.runtimeLastUnitLostSoundTick
            >= lossWarningCooldown) {
        emitRuntimeSound(state, runtimeSoundIntentUnitLost);
        state.runtimeLastUnitLostSoundTick = state.tick;
    }

    const int terminalWeaponSpawned =
        projectile.particleDamagePulse
            || timedBombRelationshipWarhead
            || psychedelicImpact
        ? 0
        : spawnRuntimeSpawnedMissileTerminalWeapon(
            state,
            projectile,
            impactCell,
            impactLevel
        );
    const int shrapnelSpawned =
        projectile.shrapnelReleased || projectile.particleDamagePulse
            || timedBombRelationshipWarhead
            || psychedelicImpact
        ? 0
        : spawnRuntimeProjectileShrapnel(
            state,
            projectile,
            impactCell,
            impactLevel
        );
    const int airburstSpawned =
        projectile.particleDamagePulse
            || timedBombRelationshipWarhead
            || psychedelicImpact
        ? 0
        : spawnRuntimeProjectileAirburst(
            state,
            projectile,
            impactCell,
            impactLevel
        );

    state.simulationStatus =
        "impact #"
        + std::to_string(projectile.attackerObjectId)
        + " -> #"
        + std::to_string(projectile.targetObjectId)
        + " damaged "
        + std::to_string(damagedObjects)
        + " total "
        + std::to_string(totalDamage)
        + " destroyed "
        + std::to_string(destroyedObjects)
        + " terrain "
        + std::to_string(terrainDamage.objectsDamaged)
        + "/"
        + std::to_string(terrainDamage.objectsDestroyed)
        + " walls "
        + std::to_string(overlayWallDamage.objectsDamaged)
        + "/"
        + std::to_string(overlayWallDamage.objectsDestroyed)
        + " ore "
        + std::to_string(oreDamage.cellsTouched)
        + "/"
        + std::to_string(oreDamage.bailsRemoved)
        + " bridge "
        + (bridgeDamage.hit
            ? (bridgeDamage.destroyed
                ? std::string("destroyed")
                : std::to_string(bridgeDamage.healthRemaining))
            : std::string("none"))
        + " terminal "
        + std::to_string(terminalWeaponSpawned)
        + " shrapnel "
        + std::to_string(shrapnelSpawned)
        + " airburst "
        + std::to_string(airburstSpawned);

    if (state.runtimeProjectileImpactObserver) {
        state.runtimeProjectileImpactObserver(projectile, true);
    }
    return true;
}

const ParticleSystemType* runtimeVehicleDamageSmokeType(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    if (object.kind != WorldObjectKind::Unit || !object.alive || object.health <= 0
        || object.submerged || object.cloaked) return nullptr;
    const auto* unit = findRuntimeUnitType(state.rules, object);
    if (unit == nullptr || unit->strength <= 0
        || static_cast<double>(object.health) > runtimeMaxHealthForObject(state, object)
            * state.rules.getSpecialMechanismRules().conditionYellow) return nullptr;
    if (unit->missileSpawn) {
        const auto missile = state.specialMechanisms.spawnedMissiles().find(object.instanceId);
        // An active launch has a visible body while preparing on a surface
        // launcher. An unlaunched docked child and underwater preparation do not.
        if (missile == state.specialMechanisms.spawnedMissiles().end()) return nullptr;
        if (!missile->second.released) {
            const auto* parent = findRuntimeUnitTypeById(state.rules, missile->second.parentType);
            if (parent != nullptr && parent->underwater) return nullptr;
        }
    }
    // Airport aircraft retain their reserved dock while flying. This is not
    // a concealed passenger or a grounded aircraft being serviced at its pad.
    if (!unit->missileSpawn && object.dockedToObjectId >= 0
        && (!object.airborne || !runtimeUnitUsesAirMovement(*unit))) return nullptr;
    std::vector<const ParticleSystemType*> smoke;
    for (const auto& id : unit->damageParticleSystems) {
        const auto* system = state.rules.findParticleSystemType(id);
        if (system != nullptr && system->behavior == ParticleBehavior::Smoke
            && state.rules.findParticleType(system->holdsWhat) != nullptr) smoke.push_back(system);
    }
    if (smoke.empty()) return nullptr;
    // Stock vehicles have one smoke choice. Stable selection also supports
    // multiple authored Smoke systems without changing combat random state.
    return smoke[static_cast<std::uint32_t>(object.instanceId) % smoke.size()];
}

RuntimeParticlePoint runtimeVehicleDamageSmokeOrigin(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    const auto* unit = findRuntimeUnitType(state.rules, object);
    const auto offset = unit == nullptr ? std::array<int, 3>{0, 0, 0} : unit->damageSmokeOffset;
    if (unit != nullptr && unit->missileSpawn) {
        const auto missile = state.specialMechanisms.spawnedMissiles().find(object.instanceId);
        if (missile != state.specialMechanisms.spawnedMissiles().end())
            return runtimeSpawnedMissileDamageSmokePose(missile->second, offset).origin;
    }
    // Stock tanks use either the default origin or the shared heavy-vehicle
    // offset. Damage smoke rises from the top of the turret/body centre.
    // Retain individually authored outlets and the non-tank vehicle families.
    const bool stockTankOutlet = unit != nullptr && toLowerAsciiLocal(unit->category) == "afv"
        && !unit->naval && !unit->jumpJet && !unit->balloonHover
        && (offset == std::array<int, 3>{0, 0, 0}
            || offset == std::array<int, 3>{100, 100, 275});
    if (stockTankOutlet) {
        return {runtimeObjectRenderX(object) * 256.0,
            runtimeObjectRenderY(object) * 256.0,
            runtimeObjectRenderLevel(object) * 256.0 + 256.0};
    }
    // Retail GetParticleSysOffset (0x7178C0) returns these offsets in world
    // axes by default; rotating them with the turret would move the smoke source.
    return {runtimeObjectRenderX(object) * 256.0 + offset[0],
        runtimeObjectRenderY(object) * 256.0 + offset[1],
        runtimeObjectRenderLevel(object) * 256.0 + offset[2]};
}

int runtimeVehicleDamageSmokeScreenOffset(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const auto* unit = findRuntimeUnitType(state.rules, object);
    if (unit != nullptr && unit->missileSpawn) return 0;
    RuntimeMapRenderContext context;
    context.rules = &state.rules;
    // Magnetic lifting also marks ground vehicles airborne. Use the body's
    // aircraft classification so those vehicles do not gain an extra offset.
    return -runtimeAircraftSpriteLiftPixels(object,
        runtimeObjectUsesAircraftSpriteForRender(object, context));
}

RuntimeParticleSpriteAttachment runtimeVehicleDamageSmokeSpriteAttachment(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    const auto* unit = findRuntimeUnitType(state.rules, object);
    if (unit != nullptr && unit->missileSpawn) {
        const auto missile = state.specialMechanisms.spawnedMissiles().find(object.instanceId);
        if (missile != state.specialMechanisms.spawnedMissiles().end())
            return runtimeSpawnedMissileSpriteAttachment(missile->second);
    }
    return {};
}

bool runtimeVehicleDamageSmokePresentation(
    RuntimeProductionHostState& state, const WorldObjectInstance& object, int screenOffsetY,
    RuntimeParticleAirbornePresentation& presentation, std::string& error,
    std::unique_ptr<RuntimeSessionAirPresentationBatch>* sharedBatch) {
    presentation = {};
    error.clear();
    if (screenOffsetY >= 0) return true;
    if (!runtimeSessionGeometryAvailable(state)) return true;
    if (sharedBatch != nullptr && *sharedBatch)
        return (*sharedBatch)->capture(object, presentation, &error);
    RuntimeMapRenderContext context;
    context.rules = &state.rules;
    context.art = state.artLoaded ? &state.art : nullptr;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.animationTick = state.tick;
    context.fixedTickHz = state.config.fixedTickHz;
    if (sharedBatch != nullptr) {
        *sharedBatch = state.geometry->beginAircraftBatch(
            state.world, context, state.renderCache);
        return (*sharedBatch)->capture(object, presentation, &error);
    }
    return state.geometry->captureRuntimeAircraftAirbornePresentation(state.world, object, context,
        state.renderCache, presentation, &error);
}

void reportRuntimeVehicleSmokePresentationFailure(
    RuntimeGameSessionState& state, int sourceId, const std::string& error) {
    const std::string warning = "vehicle damage smoke presentation rejected: source="
        + std::to_string(sourceId) + " " + error;
    if (std::find(state.report.warnings.begin(), state.report.warnings.end(), warning)
        == state.report.warnings.end()) {
        state.report.warnings.push_back(warning);
        std::cerr << warning << '\n';
    }
}

void stopRuntimeVehicleDamageSmokeForSource(RuntimeGameSessionState& state, int sourceObjectId) {
    if (!state.particlesInitialized) return;
    std::vector<std::uint64_t> stopped;
    for (const auto& system : state.particles.systems()) {
        if (system.emitting && system.sourceObjectId == sourceObjectId
            && system.sourceTag == "vehicle-damage-smoke") stopped.push_back(system.id);
    }
    // stopSystem may remove an empty producer, so do not mutate during traversal.
    for (auto id : stopped) (void)state.particles.stopSystem(id);
}

bool syncRuntimeVehicleDamageSmoke(RuntimeProductionHostState& state) {
    constexpr std::string_view tag = "vehicle-damage-smoke";
    std::map<int, std::uint64_t> active;
    std::vector<std::uint64_t> stopped;
    // Lazily build one world-coordinate structure index for all ordinary air
    // sources in this update. Ground units and generated missiles need none.
    std::unique_ptr<RuntimeSessionAirPresentationBatch> presentationBatch;
    bool changed = false;
    for (const auto& system : state.particles.systems()) {
        if (!system.emitting || system.sourceTag != tag) continue;
        const auto* object = findObjectById(state, system.sourceObjectId);
        const auto* expected = object == nullptr ? nullptr : runtimeVehicleDamageSmokeType(state, *object);
        if (expected == nullptr || expected->id != system.typeId || active.contains(system.sourceObjectId)) {
            stopped.push_back(system.id);
        } else {
            active.emplace(system.sourceObjectId, system.id);
            const auto origin = runtimeVehicleDamageSmokeOrigin(state, *object);
            const int screenOffsetY = runtimeVehicleDamageSmokeScreenOffset(state, *object);
            RuntimeParticleAirbornePresentation presentation;
            std::string presentationError;
            if (!runtimeVehicleDamageSmokePresentation(state, *object, screenOffsetY,
                    presentation, presentationError, &presentationBatch)) {
                stopped.push_back(system.id);
                reportRuntimeVehicleSmokePresentationFailure(state, object->instanceId, presentationError);
                continue;
            }
            const auto spriteAttachment = runtimeVehicleDamageSmokeSpriteAttachment(state, *object);
            if (origin.x != system.origin.x || origin.y != system.origin.y
                || origin.z != system.origin.z || object->owner != system.owner
                || screenOffsetY != system.screenOffsetY || presentation != system.presentation
                || spriteAttachment != system.spriteAttachment) {
                changed = state.particles.moveSystem(system.id, origin, object->owner,
                    screenOffsetY, presentation, spriteAttachment) || changed;
            }
        }
    }
    for (auto id : stopped) changed = state.particles.stopSystem(id) || changed;
    for (const auto& object : state.world.getObjects()) {
        if (active.contains(object.instanceId)) continue;
        const auto* type = runtimeVehicleDamageSmokeType(state, object);
        if (type == nullptr) continue;
        RuntimeParticleSpawnContext context;
        context.sourceObjectId = object.instanceId;
        context.owner = object.owner;
        context.sourceTag = tag;
        context.position = runtimeVehicleDamageSmokeOrigin(state, object);
        context.screenOffsetY = runtimeVehicleDamageSmokeScreenOffset(state, object);
        std::string presentationError;
        if (!runtimeVehicleDamageSmokePresentation(state, object, context.screenOffsetY,
                context.presentation, presentationError, &presentationBatch)) {
            reportRuntimeVehicleSmokePresentationFailure(state, object.instanceId, presentationError);
            continue;
        }
        context.spriteAttachment = runtimeVehicleDamageSmokeSpriteAttachment(state, object);
        context.direction = {0.0, 0.0, 1.0};
        context.seed = static_cast<std::uint32_t>(object.instanceId)
            ^ (static_cast<std::uint64_t>(state.tick) << 32U) ^ 0x64616d616765736dULL;
        changed = (state.particles.spawnSystem(state.rules, type->id, context) != 0) || changed;
    }
    return changed;
}

bool applyRuntimeAirstrikeActionsAtomically(
    RuntimeProductionHostState& state,
    const RuntimeAirstrikeSystemState& airstrikeCandidate,
    const std::vector<RuntimeAirstrikeAction>& actions) {
    std::map<int, RuntimeAirstrikePlaneBinding> bindingShadow =
        state.airstrikePlanesByToken;
    std::map<int, RuntimeAirstrikeRootAuthority> authorityShadow =
        state.airstrikeRootAuthoritiesByActor;
    std::vector<RuntimePreparedAirstrikeHostAction> prepared;
    prepared.reserve(actions.size());
    std::vector<WorldObjectInstance> spawnedPlanes;
    spawnedPlanes.reserve(actions.size());
    std::unordered_map<int, std::size_t> spawnedPlaneIndexById;

    int nextObjectId = nextRuntimeObjectInstanceId(state);
    std::unordered_set<int> usedProjectileActorIds;
    usedProjectileActorIds.reserve(
        state.projectiles.size()
            + state.recentExpiredProjectiles.size()
            + state.impactEffects.size()
            + authorityShadow.size()
            + actions.size()
    );
    for (const RuntimeProjectileEffect& projectile : state.projectiles) {
        usedProjectileActorIds.insert(projectile.actorId);
    }
    for (const RuntimeProjectileEffect& projectile :
            state.recentExpiredProjectiles) {
        usedProjectileActorIds.insert(projectile.actorId);
    }
    for (const RuntimeImpactEffect& impact : state.impactEffects) {
        usedProjectileActorIds.insert(impact.projectileActorId);
    }
    for (const auto& [rootActorId, authority] : authorityShadow) {
        (void)authority;
        usedProjectileActorIds.insert(rootActorId);
    }
    int nextProjectileActorId = state.nextProjectileActorId;
    const auto reserveProjectileActorId = [&]() -> int {
        int candidate = nextProjectileActorId;
        if (candidate <= 0 || candidate >= INT_MAX) {
            candidate = 1;
        }
        const std::size_t attempts = usedProjectileActorIds.size() + 1U;
        for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
            if (usedProjectileActorIds.insert(candidate).second) {
                nextProjectileActorId = candidate >= INT_MAX - 1
                    ? 1
                    : candidate + 1;
                return candidate;
            }
            candidate = candidate >= INT_MAX - 1 ? 1 : candidate + 1;
        }
        return -1;
    };
    const auto textMatches = [](const std::string& left,
                                const std::string& right) {
        return toLowerAsciiLocal(trimAsciiLocal(left))
            == toLowerAsciiLocal(trimAsciiLocal(right));
    };
    const auto shadowObject = [&](int objectId)
        -> const WorldObjectInstance* {
        const auto spawned = spawnedPlaneIndexById.find(objectId);
        if (spawned != spawnedPlaneIndexById.end()
            && spawned->second < spawnedPlanes.size()) {
            return &spawnedPlanes[spawned->second];
        }
        return findRuntimeObjectByIdIncludingInactive(state, objectId);
    };
    const auto pairAlreadyAuthorized = [&](int strikeId, int planeToken) {
        return std::any_of(
            authorityShadow.begin(),
            authorityShadow.end(),
            [&](const auto& entry) {
                return entry.second.strikeId == strikeId
                    && entry.second.planeToken == planeToken;
            }
        );
    };

    for (const RuntimeAirstrikeAction& action : actions) {
        RuntimePreparedAirstrikeHostAction hostAction;
        hostAction.action = action;
        if (action.kind == RuntimeAirstrikeActionKind::TargetMarked
            || action.kind == RuntimeAirstrikeActionKind::CancelStrike
            || action.kind
                == RuntimeAirstrikeActionKind::StartCallerCooldown) {
            prepared.push_back(std::move(hostAction));
            continue;
        }

        const RuntimeAirstrikeStrikeState* strike = nullptr;
        const RuntimeAirstrikePlaneState* plane =
            runtimeAirstrikePlaneStateForToken(
                airstrikeCandidate,
                action.planeToken,
                &strike
            );
        if (action.planeToken <= 0
            || (action.kind != RuntimeAirstrikeActionKind::DespawnAircraft
                && (plane == nullptr || strike == nullptr))) {
            return false;
        }

        if (action.kind == RuntimeAirstrikeActionKind::SpawnAircraft) {
            if (bindingShadow.find(action.planeToken)
                    != bindingShadow.end()
                || nextObjectId <= 0 || nextObjectId >= INT_MAX
                || strike->strikeId != action.strikeId
                || strike->callerObjectId != action.callerObjectId
                || strike->targetObjectId != action.targetObjectId
                || !textMatches(strike->owner, action.owner)
                || !textMatches(
                    strike->aircraftTypeId,
                    action.aircraftTypeId
                )
                || !textMatches(strike->weaponId, action.weaponId)
                || !textMatches(strike->warheadId, action.warheadId)) {
                return false;
            }
            const RuntimeAirstrikeAircraftDefinition definition =
                runtimeAirstrikeAircraftDefinition(
                    state,
                    action.aircraftTypeId,
                    action.elite
                );
            const UnitType* aircraft = state.rules.findUnitType(
                "AircraftType",
                action.aircraftTypeId
            );
            if (!definition.exists
                || !definition.isAircraft
                || !definition.hasAttackWeapon
                || !textMatches(definition.weaponId, action.weaponId)
                || !textMatches(definition.warheadId, action.warheadId)
                || aircraft == nullptr) {
                return false;
            }

            WorldObjectInstance object;
            object.instanceId = nextObjectId++;
            object.kind = WorldObjectKind::Unit;
            object.owner = action.owner;
            object.type = action.aircraftTypeId;
            object.health = std::max(1, aircraft->strength);
            object.x = action.route.entry.x;
            object.y = action.route.entry.y;
            object.cell = runtimeCellKey(object.x, object.y);
            object.authoredAircraft = true;
            object.alive = true;
            object.veterancy = action.elite ? 2 : 0;
            object.airborne = true;
            object.flightLevel = terrainLevelAtRuntimeCell(
                state,
                object.x,
                object.y
            ) + runtimeUnitFlightLevelOffset(*aircraft);
            object.level = object.flightLevel;
            object.onBridge = false;
            object.facing = runtimeFacingFromDelta(
                action.route.target.x - action.route.entry.x,
                action.route.target.y - action.route.entry.y
            );
            object.turretFacing = -1;
            object.subCell = -1;
            object.mission = "AirstrikeIngress";
            object.hasRenderPosition = true;
            object.renderX = static_cast<double>(object.x);
            object.renderY = static_cast<double>(object.y);
            object.renderLevel = static_cast<double>(object.level);
            object.renderOnBridge = false;
            object.renderMoveActive = true;
            initializeRuntimeObjectAmmo(object, aircraft, true);
            hostAction.objectId = object.instanceId;
            spawnedPlaneIndexById.emplace(
                object.instanceId,
                spawnedPlanes.size()
            );
            spawnedPlanes.push_back(std::move(object));

            RuntimeAirstrikePlaneBinding binding;
            binding.strikeId = action.strikeId;
            binding.planeToken = action.planeToken;
            binding.objectId = hostAction.objectId;
            binding.callerObjectId = action.callerObjectId;
            binding.targetObjectId = action.targetObjectId;
            binding.spawnTick = plane->spawnTick;
            binding.bombTick = plane->bombTick;
            binding.egressStartTick = plane->bombTick;
            binding.despawnTick = plane->despawnTick;
            binding.elite = action.elite;
            binding.owner = action.owner;
            binding.aircraftTypeId = action.aircraftTypeId;
            binding.weaponId = action.weaponId;
            binding.warheadId = action.warheadId;
            binding.route = action.route;
            bindingShadow.emplace(action.planeToken, std::move(binding));
        } else {
            auto binding = bindingShadow.find(action.planeToken);
            if (binding == bindingShadow.end()
                || binding->second.strikeId != action.strikeId) {
                return false;
            }
            hostAction.objectId = binding->second.objectId;
            const WorldObjectInstance* object = shadowObject(
                hostAction.objectId
            );
            if (object == nullptr
                || !textMatches(object->owner, binding->second.owner)
                || !textMatches(object->type, binding->second.aircraftTypeId)) {
                return false;
            }
            if (action.kind == RuntimeAirstrikeActionKind::DropBomb) {
                const RuntimeAirstrikeProjectileReceipt* receipt =
                    airstrikeCandidate.findRootProjectileReceipt(
                        action.strikeId,
                        action.planeToken
                    );
                const WorldObjectInstance* target = findObjectById(
                    state,
                    action.targetObjectId
                );
                const WeaponType* weapon = findRuntimeWeaponType(
                    state.rules,
                    action.weaponId
                );
                const WarheadType* warhead = weapon == nullptr
                    ? nullptr
                    : findRuntimeWarheadType(state.rules, weapon->warhead);
                if (!object->alive || object->health <= 0
                    || target == nullptr
                    || receipt == nullptr
                    || receipt->launchTick != action.tick
                    || receipt->terminalTick >= 0
                    || pairAlreadyAuthorized(
                        action.strikeId,
                        action.planeToken
                    )
                    || weapon == nullptr || warhead == nullptr
                    || !textMatches(weapon->id, binding->second.weaponId)
                    || !textMatches(warhead->id, binding->second.warheadId)) {
                    return false;
                }
                const int rootActorId = reserveProjectileActorId();
                if (rootActorId <= 0) {
                    return false;
                }
                hostAction.rootProjectileActorId = rootActorId;
                binding->second.route.target = action.route.target;
                RuntimeAirstrikeRootAuthority authority;
                authority.rootProjectileActorId = rootActorId;
                authority.strikeId = action.strikeId;
                authority.planeToken = action.planeToken;
                authority.planeObjectId = object->instanceId;
                authority.callerObjectId = action.callerObjectId;
                authority.targetObjectId = action.targetObjectId;
                authority.launchTick = action.tick;
                authority.elite = action.elite;
                authority.bombCell = action.route.target;
                authority.owner = action.owner;
                authority.aircraftTypeId = action.aircraftTypeId;
                authority.weaponId = action.weaponId;
                authority.warheadId = action.warheadId;
                authority.projectileId = weapon->projectile;
                authority.veteranDamageMultiplier =
                    runtimeVeterancyAbilityMultiplier(
                        state,
                        *object,
                        VeteranAbility::Firepower
                    );
                authority.damage = runtimeAuthoredDamageAgainst(
                    state,
                    *target,
                    weapon->damage,
                    weapon->warhead,
                    0.0,
                    authority.veteranDamageMultiplier
                );
                authority.rounds.push_back({rootActorId, action.tick, -1,
                    authority.bombCell, authority.damage});
                authority.roundsRemaining = std::clamp(weapon->burst, 1, 256) - 1;
                // Schedule after committing the first missile so the shared
                // burst clock uses the same deterministic actor cursor.
                authority.nextFireTick = authority.roundsRemaining > 0 ? action.tick : -1;
                authorityShadow.emplace(rootActorId, std::move(authority));
            } else if (action.kind
                    == RuntimeAirstrikeActionKind::BeginEgress) {
                const bool hasBombAuthority = pairAlreadyAuthorized(
                    action.strikeId,
                    action.planeToken
                );
                if (!hasBombAuthority) {
                    const auto* current=shadowObject(binding->second.objectId);
                    if(current==nullptr||!current->alive||!current->airborne||!current->hasRenderPosition
                        ||!std::isfinite(current->renderX)||!std::isfinite(current->renderY))return false;
                    // Preserve the actual pose already shown before this action.
                    // action.tick can be later than the last rendered route tick.
                    binding->second.hasPreciseEgressOrigin=true;
                    binding->second.preciseEgressOriginX=current->renderX;
                    binding->second.preciseEgressOriginY=current->renderY;
                    binding->second.route.target.x=static_cast<int>(std::lround(current->renderX));
                    binding->second.route.target.y=static_cast<int>(std::lround(current->renderY));
                }
                binding->second.egressStartTick = action.tick;
                binding->second.despawnTick = plane->despawnTick;
            } else if (action.kind
                    == RuntimeAirstrikeActionKind::DespawnAircraft) {
                bindingShadow.erase(binding);
            }
        }
        prepared.push_back(std::move(hostAction));
    }

    std::unordered_set<int> boundObjectIds;
    for (auto binding = bindingShadow.begin();
         binding != bindingShadow.end();) {
        const RuntimeAirstrikeStrikeState* strike = nullptr;
        const RuntimeAirstrikePlaneState* plane =
            runtimeAirstrikePlaneStateForToken(
                airstrikeCandidate,
                binding->first,
                &strike
            );
        const WorldObjectInstance* object = shadowObject(
            binding->second.objectId
        );
        if (plane == nullptr || strike == nullptr) {
            if (object != nullptr && object->alive && object->health > 0) {
                return false;
            }
            binding = bindingShadow.erase(binding);
            continue;
        }
        if (plane->phase == RuntimeAirstrikePlanePhase::Destroyed
            || plane->phase == RuntimeAirstrikePlanePhase::Terminated) {
            if (object != nullptr && object->alive && object->health > 0) {
                return false;
            }
            binding = bindingShadow.erase(binding);
            continue;
        }
        if ((plane->phase != RuntimeAirstrikePlanePhase::Ingress
                && plane->phase != RuntimeAirstrikePlanePhase::Egress)
            || strike->strikeId != binding->second.strikeId
            || object == nullptr
            || !object->alive
            || !boundObjectIds.insert(binding->second.objectId).second) {
            return false;
        }
        ++binding;
    }

    for (WorldObjectInstance& object : spawnedPlanes) {
        state.cloakObservedHealthByObject[object.instanceId] = object.health;
        state.world.accessObjects().push_back(std::move(object));
    }
    state.airstrikePlanesByToken = std::move(bindingShadow);
    state.airstrikeRootAuthoritiesByActor = std::move(authorityShadow);
    state.nextProjectileActorId = nextProjectileActorId;

    for (const RuntimePreparedAirstrikeHostAction& hostAction : prepared) {
        const RuntimeAirstrikeAction& action = hostAction.action;
        WorldObjectInstance* object = hostAction.objectId <= 0
            ? nullptr
            : findMutableObjectById(state, hostAction.objectId);
        if (action.kind == RuntimeAirstrikeActionKind::AssignIngressRoute
            && object != nullptr) {
            object->mission = "AirstrikeIngress";
        } else if (action.kind
                == RuntimeAirstrikeActionKind::BeginEgress
            && object != nullptr) {
            object->mission = "AirstrikeEgress";
        } else if (action.kind == RuntimeAirstrikeActionKind::DropBomb
            && object != nullptr) {
            WorldObjectInstance* target = findMutableObjectById(
                state,
                action.targetObjectId
            );
            const WeaponType* weapon = findRuntimeWeaponType(
                state.rules,
                action.weaponId
            );
            if (target == nullptr || weapon == nullptr) {
                return false;
            }
            const RuntimeAirstrikeRootAuthority* authority =
                runtimeAirstrikeRootAuthorityForPair(
                    state,
                    action.strikeId,
                    action.planeToken
                );
            if (authority == nullptr) {
                return false;
            }
            object->x = authority->bombCell.x;
            object->y = authority->bombCell.y;
            object->cell = runtimeCellKey(object->x, object->y);
            object->renderX = static_cast<double>(object->x);
            object->renderY = static_cast<double>(object->y);
            RuntimeWeaponProfile profile;
            finalizeRuntimeWeaponProfile(
                profile,
                state,
                action.elite
                    ? RuntimeWeaponSlot::ElitePrimary
                    : RuntimeWeaponSlot::Primary,
                *weapon,
                "airstrikeManagedBomb"
            );
            spawnRuntimeProjectile(
                state,
                *object,
                *target,
                *weapon,
                profile,
                authority->damage,
                -1,
                0,
                false,
                -1,
                -1.0,
                0,
                0,
                0,
                false,
                nullptr,
                hostAction.rootProjectileActorId,
                action.strikeId,
                action.planeToken,
                airstrikeCandidate.findStrike(action.strikeId) != nullptr
                    && airstrikeCandidate.findStrike(action.strikeId)->forceFire
            );
            auto& salvo = state.airstrikeRootAuthoritiesByActor.at(hostAction.rootProjectileActorId);
            if (salvo.roundsRemaining > 0)
                salvo.nextFireTick = static_cast<int>(std::min<std::int64_t>(INT_MAX,
                    static_cast<std::int64_t>(state.tick) + runtimeNextBurstDelayTicks(state, *object, *weapon, 1)));
        } else if (action.kind
                == RuntimeAirstrikeActionKind::DespawnAircraft
            && object != nullptr) {
            RuntimeObjectExitRequest exit;
            exit.objectId = object->instanceId;
            exit.kind = RuntimeObjectExitKind::ScriptRemoved;
            exit.tick = state.tick;
            exit.mission = "AirstrikeDespawn";
            exit.reason = "airstrike egress complete";
            exit.sourceObjectId = action.callerObjectId;
            exit.sourceOwner = action.owner;
            exit.suppressDeathWeapon = true;
            (void)queueRuntimeObjectExit(state, std::move(exit));
        }
    }
    (void)drainRuntimeObjectExitQueue(state);
    invalidateRuntimeTickOccupancyCache(state);
    invalidateRuntimeObjectProjectionCache(state);
    state.repaintRequested = true;
    return true;
}

RuntimeAirstrikeTargetSnapshot runtimeAirstrikeTargetSnapshotForStrike(
    const RuntimeGameSessionState& state,
    const RuntimeAirstrikeStrikeState& strike) {
    RuntimeAirstrikeTargetSnapshot snapshot;
    snapshot.objectId = strike.targetObjectId;
    const WorldObjectInstance* target = findObjectById(
        state,
        strike.targetObjectId
    );
    if (target == nullptr || target->health <= 0) {
        return snapshot;
    }
    snapshot.cell = {target->x, target->y};
    snapshot.alive = true;
    snapshot.visibleToCaller = runtimeObjectVisibleToOwner(
        state,
        strike.owner,
        *target
    );
    const WeaponType* weapon = findRuntimeWeaponType(
        state.rules,
        strike.weaponId
    );
    snapshot.targetable = weapon != nullptr
        && (strike.forceFire || !runtimeOwnersAreAllied(state, strike.owner, target->owner))
        && runtimeWeaponCanTargetObject(state, *weapon, *target);
    return snapshot;
}

bool runtimeAirstrikeCallerMatchesStrike(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& caller,
    const RuntimeAirstrikeStrikeState& strike) {
    const UnitType* rules = findRuntimeUnitType(state.rules, caller);
    if (rules == nullptr
        || toLowerAsciiLocal(trimAsciiLocal(caller.owner))
            != toLowerAsciiLocal(trimAsciiLocal(strike.owner))) {
        return false;
    }
    const int count = strike.elite
        ? rules->eliteAirstrikeTeam
        : rules->airstrikeTeam;
    const std::string& type = strike.elite
        ? rules->eliteAirstrikeTeamType
        : rules->airstrikeTeamType;
    const int recharge = strike.elite
        ? rules->eliteAirstrikeRechargeTime
        : rules->airstrikeRechargeTime;
    return count == static_cast<int>(strike.planes.size())
        // Older saves stored authored frames directly; retain those ongoing
        // strikes while new requests use the current fixed-tick clock.
        && (recharge == strike.rechargeTicks
            || runtimeRuleFramesToTicks(recharge, state.config.fixedTickHz) == strike.rechargeTicks)
        && toLowerAsciiLocal(trimAsciiLocal(type))
            == toLowerAsciiLocal(trimAsciiLocal(strike.aircraftTypeId));
}

void updateRuntimeAirstrikePlaneRoutes(RuntimeGameSessionState& state) {
    for (const auto& [planeToken, binding] :
            state.airstrikePlanesByToken) {
        const RuntimeAirstrikePlaneState* plane =
            runtimeAirstrikePlaneStateForToken(state.airstrikes, planeToken);
        WorldObjectInstance* object = findMutableObjectById(
            state,
            binding.objectId
        );
        if (plane == nullptr || object == nullptr) {
            continue;
        }
        RuntimeAirstrikeCell from = binding.route.entry;
        RuntimeAirstrikeCell to = binding.route.target;
        int startTick = binding.spawnTick;
        int endTick = binding.bombTick;
        object->mission = "AirstrikeIngress";
        if (plane->phase == RuntimeAirstrikePlanePhase::Egress) {
            from = binding.route.target;
            to = binding.route.exit;
            startTick = binding.egressStartTick;
            endTick = binding.despawnTick;
            object->mission = "AirstrikeEgress";
        }
        const double fromX=plane->phase==RuntimeAirstrikePlanePhase::Egress&&binding.hasPreciseEgressOrigin
            ?binding.preciseEgressOriginX:double(from.x);
        const double fromY=plane->phase==RuntimeAirstrikePlanePhase::Egress&&binding.hasPreciseEgressOrigin
            ?binding.preciseEgressOriginY:double(from.y);
        const double progress = std::clamp(
            static_cast<double>(state.tick - startTick)
                / static_cast<double>(std::max(1, endTick - startTick)),
            0.0,
            1.0
        );
        const double nextX=fromX+(to.x-fromX)*progress;
        const double nextY=fromY+(to.y-fromY)*progress;
        const int facing = runtimeFacingFromDelta(
            static_cast<int>(std::lround(nextX - object->renderX)),
            static_cast<int>(std::lround(nextY - object->renderY))
        );
        if (facing >= 0) {
            object->facing = facing;
        }
        object->x = static_cast<int>(std::lround(nextX));
        object->y = static_cast<int>(std::lround(nextY));
        object->cell = runtimeCellKey(object->x, object->y);
        object->level = object->flightLevel;
        object->airborne = true;
        object->onBridge = false;
        object->hasRenderPosition = true;
        object->renderX = nextX;
        object->renderY = nextY;
        object->renderLevel = static_cast<double>(object->flightLevel);
        object->renderOnBridge = false;
        object->renderMoveActive = true;
    }
}

bool advanceRuntimeAirstrikes(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeAirstrikes", state.tick);
    if (!state.rulesLoaded
        || (state.airstrikes.callers().empty()
            && state.airstrikes.strikes().empty()
            && state.airstrikes.projectileReceipts().empty()
            && state.airstrikePlanesByToken.empty()
            && state.airstrikeRootAuthoritiesByActor.empty())) {
        return false;
    }

    reconcileRuntimeAirstrikeSalvos(state);
    RuntimeAirstrikeReconcileInput reconcileInput;
    reconcileInput.currentTick = state.tick;
    reconcileInput.validateCallers = true;
    reconcileInput.validateTargets = true;
    reconcileInput.validateAircraft = true;
    reconcileInput.validateProjectileReceipts = true;

    for (const RuntimeAirstrikeCallerState& callerState :
            state.airstrikes.callers()) {
        const WorldObjectInstance* caller = findObjectById(
            state,
            callerState.callerObjectId
        );
        bool valid = caller != nullptr && caller->health > 0;
        if (valid && callerState.activeStrikeId != 0) {
            const RuntimeAirstrikeStrikeState* strike =
                state.airstrikes.findStrike(callerState.activeStrikeId);
            valid = strike != nullptr
                && runtimeAirstrikeCallerMatchesStrike(
                    state,
                    *caller,
                    *strike
                );
        }
        if (valid) {
            reconcileInput.liveCallerObjectIds.push_back(
                callerState.callerObjectId
            );
        }
    }

    std::map<int, RuntimeAirstrikeTargetSnapshot> targetsById;
    std::map<std::string, RuntimeAirstrikeAircraftDefinition>
        aircraftByType;
    for (const RuntimeAirstrikeStrikeState& strike :
            state.airstrikes.strikes()) {
        const RuntimeAirstrikeTargetSnapshot snapshot =
            runtimeAirstrikeTargetSnapshotForStrike(state, strike);
        auto [target, inserted] = targetsById.emplace(
            strike.targetObjectId,
            snapshot
        );
        if (!inserted) {
            target->second.alive = target->second.alive && snapshot.alive;
            target->second.visibleToCaller =
                target->second.visibleToCaller
                && snapshot.visibleToCaller;
            target->second.targetable = target->second.targetable
                && snapshot.targetable;
        }
        RuntimeAirstrikeAircraftDefinition definition =
            runtimeAirstrikeAircraftDefinition(
                state,
                strike.aircraftTypeId,
                strike.elite
            );
        if (!definition.hasAttackWeapon
            || toLowerAsciiLocal(trimAsciiLocal(definition.weaponId))
                != toLowerAsciiLocal(trimAsciiLocal(strike.weaponId))
            || toLowerAsciiLocal(trimAsciiLocal(definition.warheadId))
                != toLowerAsciiLocal(trimAsciiLocal(strike.warheadId))) {
            definition.exists = false;
            definition.isAircraft = false;
            definition.hasAttackWeapon = false;
        }
        const std::string key = toLowerAsciiLocal(
            trimAsciiLocal(strike.aircraftTypeId)
        );
        const auto existing = aircraftByType.find(key);
        if (existing == aircraftByType.end()) {
            aircraftByType.emplace(key, std::move(definition));
        } else if (toLowerAsciiLocal(existing->second.weaponId)
                != toLowerAsciiLocal(definition.weaponId)
            || toLowerAsciiLocal(existing->second.warheadId)
                != toLowerAsciiLocal(definition.warheadId)) {
            existing->second.exists = false;
            existing->second.isAircraft = false;
            existing->second.hasAttackWeapon = false;
        }
    }
    for (const auto& entry : targetsById) {
        reconcileInput.targets.push_back(entry.second);
    }
    for (const auto& entry : aircraftByType) {
        reconcileInput.aircraft.push_back(entry.second);
    }
    // One core receipt covers the entire aircraft salvo, including the gap
    // between impacts and rounds that have not fired yet.
    for (const auto& [actorId, authority] : state.airstrikeRootAuthoritiesByActor) {
        (void)actorId;
        if (authority.terminalTick < 0)
            reconcileInput.liveRootProjectiles.push_back({authority.strikeId, authority.planeToken});
    }

    RuntimeAirstrikeSystemState candidate = state.airstrikes;
    RuntimeAirstrikeReconcileReport reconciled = candidate.reconcile(
        reconcileInput
    );
    if (reconciled.inputRejected) {
        state.simulationStatus = "airstrike reconcile rejected";
        return false;
    }

    RuntimeAirstrikeAdvanceInput advanceInput;
    advanceInput.currentTick = state.tick;
    advanceInput.targets = reconcileInput.targets;
    for (const auto& [planeToken, binding] :
            state.airstrikePlanesByToken) {
        const WorldObjectInstance* object = findObjectById(
            state,
            binding.objectId
        );
        if (object == nullptr || object->health <= 0) {
            advanceInput.destroyedPlaneTokens.push_back(planeToken);
        }
    }
    RuntimeAirstrikeAdvanceResult advanced = candidate.advance(advanceInput);
    if (advanced.inputRejected || !advanced.advanced) {
        state.simulationStatus = "airstrike advance rejected";
        return false;
    }
    std::vector<RuntimeAirstrikeAction> actions = std::move(
        reconciled.actions
    );
    actions.insert(
        actions.end(),
        std::make_move_iterator(advanced.actions.begin()),
        std::make_move_iterator(advanced.actions.end())
    );
    if (!applyRuntimeAirstrikeActionsAtomically(state, candidate, actions)) {
        state.simulationStatus =
            "airstrike host action batch rejected atomically";
        return false;
    }
    state.airstrikes = std::move(candidate);
    updateRuntimeAirstrikePlaneRoutes(state);
    advanceRuntimeAirstrikeSalvos(state);

    for (auto& [rootActorId, authority] :
            state.airstrikeRootAuthoritiesByActor) {
        (void)rootActorId;
        const RuntimeAirstrikeProjectileReceipt* receipt =
            state.airstrikes.findRootProjectileReceipt(
                authority.strikeId,
                authority.planeToken
            );
        if (authority.terminalTick < 0
            && receipt != nullptr
            && receipt->terminalTick >= 0) {
            authority.terminalTick = receipt->terminalTick;
        }
    }
    for (auto authority =
            state.airstrikeRootAuthoritiesByActor.begin();
         authority != state.airstrikeRootAuthoritiesByActor.end();) {
        const bool liveDescendant = std::any_of(
            state.projectiles.begin(),
            state.projectiles.end(),
            [&](const RuntimeProjectileEffect& projectile) {
                return projectile.airstrikeStrikeId
                        == authority->second.strikeId
                    && projectile.airstrikePlaneToken
                        == authority->second.planeToken;
            }
        );
        if (authority->second.terminalTick >= 0 && !liveDescendant) {
            authority = state.airstrikeRootAuthoritiesByActor.erase(
                authority
            );
        } else {
            ++authority;
        }
    }
    state.simulationStatus = actions.empty()
        ? "airstrike runtime advanced"
        : "airstrike host actions applied";
    return true;
}

bool runtimeChronoMoveTerrainReference() {
    if (runtimeChronoMoveTerrainReferenceOverride >= 0)
        return runtimeChronoMoveTerrainReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_CHRONO_MOVE_TERRAIN_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

std::vector<int> runtimeCombatSelection(const RuntimeProductionHostState& state) {
    auto attackerIds = state.selectedObjectIds;
    for (int& id : attackerIds) {
        const auto* selected = findObjectById(state, id);
        if (selected != nullptr && selected->tankBunkerOccupantId >= 0) id = selected->tankBunkerOccupantId;
    }
    std::sort(attackerIds.begin(), attackerIds.end());
    attackerIds.erase(std::unique(attackerIds.begin(), attackerIds.end()), attackerIds.end());
    return attackerIds;
}

bool runtimeWeaponRepairTargetLegal(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& target,
    const RuntimeWeaponProfile& profile) {
    return source.alive && source.health > 0 && target.alive && target.health > 0
        && source.instanceId != target.instanceId
        && !runtimeObjectDisabledByParasite(state, source.instanceId)
        && profile.weapon != nullptr && profile.weapon->damage < 0
        && runtimeWeaponHasAttackRange(*profile.weapon)
        && runtimeObjectHasAttackAmmo(state.rules, source)
        && runtimeOwnersAreAllied(state, source.owner, target.owner)
        && (target.health < runtimeMaxHealthForObject(state, target)
            || (runtimeActiveShield(target, state.rules, profile.warhead)
                && target.modExtensions.shieldHealth < target.modExtensions.shieldCapacity));
}

void issueRuntimeAttackCommand(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject,
    bool forceFire) {
    cleanRuntimeSelection(state);
    // Ctrl on a C4 building target keeps the demolition ability rather than
    // falling through to the infantry's ordinary gun.
    if (forceFire && queueRuntimeTimedBombTarget(state, targetObject, true)) return;
    const auto attackerIds = runtimeCombatSelection(state);
    ensureRuntimeGarrisonsInitialized(state);
    const RuntimeCellPoint targetCell = runtimeCellPointForObject(targetObject);
    state.actionSummary.formationTargets = 0;
    resetLastRuntimeCommand(
        state,
        forceFire ? "force-attack" : "target",
        targetCell,
        targetObject.instanceId,
        targetObject.type
    );

    std::unordered_set<int> attackingGroupIds;
    for (int objectId : attackerIds) {
        if (objectId == targetObject.instanceId) {
            continue;
        }
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (object != nullptr
            && object->alive
            && runtimeObjectCanMove(state, *object)) {
            attackingGroupIds.insert(objectId);
        }
    }

    int acceptedAttackIndex = 0;
    int acceptedRepairCount = 0;
    for (int objectId : attackerIds) {
        if (objectId == targetObject.instanceId) {
            ++state.lastCommand.rejectedCount;
            continue;
        }

        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr
            || runtimeObjectDisabledByParasite(state, objectId)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        const bool returnToDeployed =
            runtimeObjectHasDeployedState(state, *object);
        const bool garrisonedHost =
            object->kind == WorldObjectKind::Structure
            && !object->garrisonOccupantIds.empty();
        const RuntimeWeaponProfile weaponProfile = garrisonedHost
            ? RuntimeWeaponProfile{}
            : runtimeWeaponProfileForObject(state, *object, &targetObject, forceFire);
        const WeaponType* weapon = weaponProfile.weapon;
        if (weaponProfile.warhead != nullptr && weaponProfile.warhead->ivanBomb
            && !runtimeBuildingBombOrderRelationLegal(state, *object, targetObject, forceFire)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        if (!runtimeBridgeHutDemolitionCommandLegal(state, targetObject, weapon,
                weaponProfile.warhead, runtimeObjectHasC4Capability(state.rules, *object))) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        const bool allied = runtimeOwnersAreAllied(
            state,
            object->owner,
            targetObject.owner
        );
        const bool repair = !garrisonedHost
            && weapon != nullptr
            && weapon->damage < 0;
        const bool timedBombRelationship = !garrisonedHost
            && weaponProfile.warhead != nullptr
            && ((weaponProfile.warhead->bombDisarm
                    && state.specialMechanismsInitialized
                    && state.specialMechanisms.timedBombs().find(
                        targetObject.instanceId
                    ) != state.specialMechanisms.timedBombs().end())
                || weaponProfile.warhead->ivanBomb);
        const bool disguiseRelationship = !garrisonedHost
            && weaponProfile.warhead != nullptr
            && weaponProfile.warhead->makesDisguise;
        const bool disguiseRelationshipLegal = disguiseRelationship
            && runtimeSpyCanApplyMakesDisguise(
                state,
                object->instanceId,
                targetObject.instanceId,
                weaponProfile.warhead->id
            );
        const bool infiniteDisguiseRange = disguiseRelationship
            && disguiseRelationshipLegal
            && weapon != nullptr && weapon->range < 0.0;
        if (garrisonedHost) {
            if (!runtimeGarrisonHostCanAttackTarget(
                    state,
                    *object,
                    targetObject,
                    forceFire
                )) {
                ++state.lastCommand.rejectedCount;
                continue;
            }
        } else if (!runtimeObjectHasAttackAmmo(state.rules, *object)
            || weapon == nullptr
            || (weapon->damage == 0
                && !runtimeWeaponHasAttachEffect(state.rules, weapon)
                && !timedBombRelationship
                && !disguiseRelationshipLegal)
            || (!runtimeWeaponHasAttackRange(*weapon) && !infiniteDisguiseRange)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        if (disguiseRelationship && !disguiseRelationshipLegal) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        if (forceFire && (repair || disguiseRelationship)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        if (!forceFire && !disguiseRelationshipLegal
            && runtimeSpyDisguiseBlocksAttack(
                state,
                *object,
                targetObject,
                false
            )) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        const bool psychicAttackLegal = repair || timedBombRelationship
            || disguiseRelationshipLegal
            || runtimePsychicCanAttackTarget(
                state,
                *object,
                targetObject,
                true,
                runtimeOrdinaryAttackRelationLegal(state, *object, targetObject, forceFire)
            );
        if ((repair && (!allied
                || (targetObject.health >= runtimeMaxHealthForObject(state, targetObject) && !(runtimeActiveShield(targetObject, state.rules, weaponProfile.warhead) && targetObject.modExtensions.shieldHealth < targetObject.modExtensions.shieldCapacity))))
            || (!repair && !timedBombRelationship
                && !disguiseRelationshipLegal
                && !psychicAttackLegal)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        const double distanceToTarget = garrisonedHost
            ? runtimeCellDistanceToObjectFootprint(
                state,
                runtimeCellPointForObject(*object),
                targetObject
            )
            : runtimeWeaponDistanceToTarget(
                state,
                *object,
                targetObject,
                *weapon
            );
        const double attackRange = garrisonedHost
            ? state.garrisons.config().occupyWeaponRange
            : (infiniteDisguiseRange
                ? std::numeric_limits<double>::infinity()
                : runtimeEffectiveWeaponRange(
                    state,
                    *object,
                    targetObject,
                    *weapon
                ));
        const bool hasLineOfSight = garrisonedHost
            || runtimeWeaponHasLineOfSight(
                state,
                *object,
                targetObject,
                *weapon
            );
        if ((distanceToTarget > attackRange || !hasLineOfSight)
            && !runtimeObjectCanMove(state, *object)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }

        if (!releaseRuntimeRepairDepotForCommand(
                state,
                objectId,
                "manual attack command"
            )) {
            ++state.lastCommand.rejectedCount;
            continue;
        }

        departRuntimeAircraftDock(state, *object);
        const auto oldBehavior = state.behaviors.find(objectId);
        const int inheritedWeaponCooldown =
            oldBehavior != state.behaviors.end()
            ? std::max(0, oldBehavior->second.weaponCooldownTicks)
            : 0;
        if (oldBehavior != state.behaviors.end()) {
            clearRuntimeTemporalAttack(
                state,
                objectId,
                oldBehavior->second,
                "new attack command interrupted attack"
            );
        }
        const RuntimeAirMoveContinuation flightContinuation(
            *object, oldBehavior != state.behaviors.end() ? &oldBehavior->second : nullptr);
        std::optional<RuntimeObjectBehavior> surfaceContinuation;
        // The next cell and its infantry subcell are already committed. A new
        // attack changes the goal after this segment, without finishing it on
        // the click or restoring an obsolete destination reservation.
        if (oldBehavior != state.behaviors.end() && oldBehavior->second.moveCooldownTicks > 0
            && runtimeObjectMovementMode(state, *object) != PathMovementMode::Air)
            surfaceContinuation = oldBehavior->second;
        const bool cancellingStructureEntry = runtimeCancelledStructureEntryNeedsRecovery(state, *object);
        if (!eraseRuntimeBehavior(state, objectId)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        if (cancellingStructureEntry) surfaceContinuation.reset();
        RuntimeObjectBehavior behavior;
        behavior.kind = RuntimeBehaviorKind::Attack;
        behavior.state =
            distanceToTarget > attackRange || !hasLineOfSight
                ? RuntimeBehaviorState::Chasing
                : RuntimeBehaviorState::Queued;
        behavior.targetObjectId = targetObject.instanceId;
        behavior.forceFire = forceFire;
        behavior.weaponCooldownTicks = inheritedWeaponCooldown;
        behavior.returnToDeployed = returnToDeployed;
        behavior.approachSlotIndex = acceptedAttackIndex;
        if (!attackingGroupIds.empty()) {
            behavior.pendingIgnoredObjectIds = attackingGroupIds;
        }
        flightContinuation.restore(*object, behavior);
        if (surfaceContinuation) inheritRuntimeVehicleMoveSegment(*object, behavior, *surfaceContinuation);
        if (distanceToTarget > attackRange && !returnToDeployed) {
            setRuntimeMissionIntent(
                state,
                *object,
                RuntimeMissionIntent::Idle,
                "attackChaseQueued"
            );
            behavior.nextRepathTick =
                std::max(
                    behavior.nextRepathTick,
                    state.tick
                        + (acceptedAttackIndex
                            / std::max(1, runtimeMaxReadyPathRequestsPerTick))
                );
        } else {
            if (garrisonedHost) {
                setRuntimeMissionIntent(
                    state,
                    *object,
                    RuntimeMissionIntent::Occupying,
                    "garrisonAttackQueued"
                );
            } else if (returnToDeployed) {
                setRuntimeMissionIntent(
                    state,
                    *object,
                    RuntimeMissionIntent::Deployed,
                    "attackQueuedDeployed"
                );
            } else {
                setRuntimeMissionIntent(
                    state,
                    *object,
                    RuntimeMissionIntent::Idle,
                    "attackQueued"
                );
            }
        }
        if (returnToDeployed) {
            object->turretFacing = object->facing;
        }
        state.behaviors[object->instanceId] = behavior;
        ensureRuntimeSpecialMechanismsInitialized(state);
        state.specialMechanisms.notifyActivity(
            object->instanceId,
            state.tick,
            "attack", 0
        );
        if (repair) {
            ++acceptedRepairCount;
        }
        ++acceptedAttackIndex;
        ++state.lastCommand.acceptedCount;
    }

    state.interactionStatus =
        state.lastCommand.acceptedCount > 0
            ? (acceptedRepairCount > 0
                ? "repair command accepted"
                : "target command accepted")
            : "target command rejected";
    if (state.lastCommand.acceptedCount > 0
        && !state.runtimeSuppressPlayerAudioFeedback) {
        if (acceptedRepairCount > 0) {
            const std::string repairVoice =
                trimAsciiLocal(state.rules.getVoiceIFVRepair());
            emitRuntimeSound(
                state,
                repairVoice.empty() ? "command target" : repairVoice
            );
        } else {
            emitRuntimeSound(
                state,
                runtimeVoiceEventForSelectedUnits(
                    state,
                    "attack",
                    &targetObject,
                    forceFire
                ).value_or("command target")
            );
        }
    } else if (state.lastCommand.acceptedCount <= 0
        && !state.runtimeSuppressPlayerAudioFeedback) {
        emitRuntimeSound(state, "blocked");
    }
    recordRuntimeCommandReplayEvent(state);
    refreshRuntimeActionSummary(state);
}

int runtimeTransportReservedPassengerCount(
    const RuntimeGameSessionState& state,
    int transportId,
    int excludedPassengerId) {
    int count = 0;
    for (const auto& [passengerId, targetId] :
            state.pendingTransportTargets) {
        if (targetId != transportId || passengerId == excludedPassengerId) {
            continue;
        }
        const WorldObjectInstance* passenger =
            findObjectById(state, passengerId);
        const WorldObjectInstance* transport =
            findObjectById(state, targetId);
        const auto behaviorIt = state.behaviors.find(passengerId);
        if (passenger != nullptr
            && transport != nullptr
            && behaviorIt != state.behaviors.end()
            && behaviorIt->second.kind == RuntimeBehaviorKind::Move) {
            count += runtimeTransportPassengerSpace(state, *transport, *passenger);
        }
    }
    return count;
}

void handleRuntimeGarrisonHostDestruction(
    RuntimeProductionHostState& state,
    int hostObjectId) {
    ensureRuntimeGarrisonsInitialized(state);
    cancelRuntimePendingOccupyForTarget(
        state,
        hostObjectId,
        "garrisonTargetDestroyed"
    );
    WorldObjectInstance* host =
        findMutableRuntimeObjectByIdIncludingInactive(state, hostObjectId);
    if (host == nullptr || host->garrisonOccupantIds.empty()) {
        return;
    }
    const std::vector<RuntimeGarrisonEvacuationAction> actions =
        state.garrisons.handleHostDestruction(
            state.world.accessObjects(),
            hostObjectId,
            true
        );
    for (const RuntimeGarrisonEvacuationAction& action : actions) {
        eraseRuntimeBehavior(state, action.occupantId);
        removeSelectedObject(state, action.occupantId);
        state.pendingOccupyTargets.erase(action.occupantId);
        if (WorldObjectInstance* occupant =
                findMutableRuntimeObjectByIdIncludingInactive(
                    state,
                    action.occupantId
                )) {
            occupant->subCell = -1;
        }
    }
    if (!actions.empty()) {
        eraseRuntimeBehavior(state, hostObjectId);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        state.navigationRefreshPending = true;
        invalidateRuntimeObjectProjectionCache(state);
    }
}

bool advanceRuntimeGarrisonState(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeGarrisonState", state.tick);
    if (!state.rulesLoaded) {
        return false;
    }
    ensureRuntimeGarrisonsInitialized(state);
    std::vector<int> hostIds;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.garrisonOccupantIds.empty()) {
            hostIds.push_back(object.instanceId);
        }
    }
    std::sort(hostIds.begin(), hostIds.end());

    bool changed = false;
    for (int hostId : hostIds) {
        WorldObjectInstance* host =
            findMutableRuntimeObjectByIdIncludingInactive(state, hostId);
        if (host == nullptr) {
            continue;
        }
        if (!host->alive || host->health <= 0) {
            const std::size_t occupantsBefore =
                host->garrisonOccupantIds.size();
            handleRuntimeGarrisonHostDestruction(state, hostId);
            changed = occupantsBefore > 0 || changed;
            continue;
        }
        state.garrisons.syncHostOccupants(
            state.world.accessObjects(),
            hostId
        );
        const std::vector<RuntimeGarrisonEvacuationAction> actions =
            evacuateRuntimeGarrisonHost(
                state,
                hostId,
                RuntimeGarrisonEvacuationReason::ConditionRed,
                true
            );
        changed = !actions.empty() || changed;
        host = findMutableRuntimeObjectByIdIncludingInactive(state, hostId);
        if (host != nullptr && !host->garrisonOccupantIds.empty()
            && toLowerAsciiLocal(host->mission) != "occupying") {
            setRuntimeMissionIntent(
                state,
                *host,
                RuntimeMissionIntent::Occupying,
                "garrisonManifestSync"
            );
            changed = true;
        }
    }
    return changed;
}

const BuildingType* runtimeTankBunkerType(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const auto* type = object.kind == WorldObjectKind::Structure
        ? state.rules.findBuildingType(object.type) : nullptr;
    return type != nullptr && type->bunker ? type : nullptr;
}

bool runtimeObjectIsTurningInTankBunker(const RuntimeGameSessionState& state,
    const WorldObjectInstance& tank) {
    const auto* host = tank.tankBunkerHostId >= 0
        ? findObjectById(state, tank.tankBunkerHostId) : nullptr;
    return host != nullptr && host->tankBunkerOccupantId == tank.instanceId
        && runtimeTankBunkerWaitingForFacing(*host);
}

bool runtimeObjectCanEnterTankBunker(const RuntimeGameSessionState& state,
    const WorldObjectInstance& tank, const WorldObjectInstance& host, bool reserve) {
    const auto* type = findRuntimeUnitType(state.rules, tank);
    if (runtimeTankBunkerType(state, host) == nullptr || !host.alive || host.health <= 0
        || host.rubble || host.buildAnimTicksRemaining > 0 || host.tankBunkerOccupantId >= 0
        || host.tankBunkerLowering || !tank.alive || tank.health <= 0
        || tank.kind != WorldObjectKind::Unit || type == nullptr || !type->bunkerable
        || tank.airborne || type->naval || tank.tankBunkerHostId >= 0
        || tank.dockedToObjectId >= 0 || tank.deployStateTicksRemaining >= 0
        || runtimeObjectHasDeployedState(state, tank)
        || runtimeObjectSuspendedByMagBeam(state, tank.instanceId)
        || runtimeObjectDisabledByParasite(state, tank.instanceId)
        || !runtimeOwnersAreAllied(state, tank.owner, host.owner)) return false;
    if (reserve) for (const auto& [id, target] : state.pendingTransportTargets) {
        if (id != tank.instanceId && target == host.instanceId) return false;
    }
    return true;
}

std::optional<RuntimeCellPoint> runtimeTankBunkerEntryCell(RuntimeProductionHostState& state,
    const WorldObjectInstance& tank, const WorldObjectInstance& host) {
    const auto cells = runtimeStructureFootprintCellsForObject(state, host);
    if (cells.empty()) return std::nullopt;
    // Drive into the foundation; never complete entry merely by proximity.
    const auto destination = cells.back();
    const auto origin = runtimeCellPointForObject(tank);
    if (origin == destination || !findRuntimePath(state, tank, origin, destination,
            host.instanceId).empty()) return destination;
    return std::nullopt;
}

void positionRuntimeBunkeredTank(RuntimeGameSessionState& state, WorldObjectInstance& tank,
    const WorldObjectInstance& host) {
    const auto cells = runtimeStructureFootprintCellsForObject(state, host);
    if (cells.empty()) return;
    double x = 0.0, y = 0.0;
    for (const auto& cell : cells) { x += cell.x; y += cell.y; }
    tank.x = cells.back().x; tank.y = cells.back().y;
    tank.cell = runtimeCellKey(tank.x, tank.y);
    tank.level = host.level; tank.onBridge = host.onBridge;
    tank.hasRenderPosition = true; tank.renderMoveActive = false;
    // The building art's interior is centered on the full foundation extent.
    // Cell indices alone put the hull half a tile above the floor, where it
    // covers the rear wall and misses the authored foreground wall mask.
    tank.renderX = x / cells.size() + 0.5;
    tank.renderY = y / cells.size() + 0.5;
    tank.renderLevel = host.level; tank.renderOnBridge = host.onBridge;
}

bool completeRuntimeTankBunkerEntry(RuntimeProductionHostState& state, int tankId, int hostId) {
    auto* tank = findMutableObjectById(state, tankId);
    auto* host = findMutableObjectById(state, hostId);
    if (tank == nullptr || host == nullptr
        || !runtimeObjectCanEnterTankBunker(state, *tank, *host, false)) {
        state.pendingTransportTargets.erase(tankId);
        return false;
    }
    const auto cells = runtimeStructureFootprintCellsForObject(state, *host);
    if (cells.empty() || runtimeCellPointForObject(*tank) != cells.back()
        || tank->renderMoveActive) return false;
    host->tankBunkerOccupantId = tankId;
    // Keep the walls lowered until the live tank has finished parking. The
    // existing negative timestamp is the saved/hashed turning stage.
    host->tankBunkerAnimationStartTick = -1;
    host->tankBunkerLowering = false;
    tank->tankBunkerHostId = hostId;
    tank->mission = "Guard";
    positionRuntimeBunkeredTank(state, *tank, *host);
    state.pendingTransportTargets.erase(tankId);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return true;
}

void releaseRuntimeTankBunker(RuntimeProductionHostState& state, WorldObjectInstance& host) {
    const bool wallsStarted = host.tankBunkerAnimationStartTick >= 0;
    if (auto* tank = findMutableRuntimeObjectByIdIncludingInactive(state, host.tankBunkerOccupantId)) {
        tank->tankBunkerHostId = -1;
        tank->bodyFacingClock = {};
        tank->turretFacingClock = {};
        if (tank->alive) tank->mission = "Guard";
    }
    host.tankBunkerOccupantId = -1;
    host.tankBunkerAnimationStartTick = wallsStarted ? state.tick : -1;
    host.tankBunkerLowering = wallsStarted;
    if (wallsStarted) emitRuntimeSound(state, "sound:" + runtimeRulesSoundSection(
        state, "BunkerWallsDownSound", "TankBunkerDown"), runtimeSpatialAudioPositionForObject(host));
}

bool unloadRuntimeTankBunker(RuntimeProductionHostState& state, WorldObjectInstance& object) {
    auto* host = object.tankBunkerHostId >= 0
        ? findMutableObjectById(state, object.tankBunkerHostId) : &object;
    if (host == nullptr || host->tankBunkerOccupantId < 0) return false;
    auto* tank = findMutableObjectById(state, host->tankBunkerOccupantId);
    if (tank == nullptr) { releaseRuntimeTankBunker(state, *host); return false; }
    std::optional<RuntimeCellPoint> exit;
    std::vector<RuntimeCellPoint> path;
    const auto foundation = runtimeStructureFootprintCellsForObject(state, *host);
    std::vector<RuntimeCellPoint> candidates;
    for (const auto& cell : foundation) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        const RuntimeCellPoint candidate{cell.x + dx, cell.y + dy};
        if (runtimeCellDistanceToObjectFootprint(state, candidate, *host) > 0.0
            && std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) candidates.push_back(candidate);
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        const double da = runtimeCellDistance(runtimeCellPointForObject(*tank), a);
        const double db = runtimeCellDistance(runtimeCellPointForObject(*tank), b);
        if (da != db) return da < db;
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    for (const auto& candidate : candidates) {
        if (!findRuntimePassableCellNear(state, *tank, candidate, 0, -1)) continue;
        path = findRuntimePath(state, *tank, runtimeCellPointForObject(*tank), candidate, host->instanceId);
        if (!path.empty()) { exit = candidate; break; }
    }
    if (!exit) return false;
    const int hostId = host->instanceId, tankId = tank->instanceId;
    releaseRuntimeTankBunker(state, *host);
    eraseRuntimeBehavior(state, tankId);
    RuntimeObjectBehavior behavior;
    behavior.kind = RuntimeBehaviorKind::Move;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.moveTarget = *exit;
    behavior.pendingAllowedOccupiedObjectId = hostId;
    replaceRuntimeBehaviorPath(behavior, path);
    tank->mission = "Move";
    state.behaviors[tankId] = std::move(behavior);
    return true;
}

bool advanceRuntimeTankBunkers(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeTankBunkers", state.tick);
    bool changed = false;
    for (auto& host : state.world.accessObjects()) {
        if (host.tankBunkerOccupantId >= 0) {
            auto* tank = findMutableRuntimeObjectByIdIncludingInactive(state, host.tankBunkerOccupantId);
            if (!host.alive || host.health <= 0 || tank == nullptr || !tank->alive || tank->health <= 0
                || tank->tankBunkerHostId != host.instanceId) {
                releaseRuntimeTankBunker(state, host); changed = true;
            } else if (!runtimeOwnersAreAllied(state, tank->owner, host.owner)) {
                // Ownership changes must never retain the former side's bonuses.
                if (!unloadRuntimeTankBunker(state, host)) releaseRuntimeTankBunker(state, host);
                changed = true;
            } else {
                positionRuntimeBunkeredTank(state, *tank, host);
                if (runtimeTankBunkerWaitingForFacing(host)) {
                    const int desiredFacing = runtimeFacingFromDelta(0, 1);
                    const int previousBody = tank->facing;
                    const int previousTurret = tank->turretFacing;
                    // Use the same authored ROT and saved angular clocks as
                    // normal movement/aiming; rendering never advances them.
                    tank->facing = runtimeAdvanceBodyFacing(state, *tank,
                        tank->facing >= 0 ? tank->facing : desiredFacing, desiredFacing);
                    bool turretReady = true;
                    if (runtimeObjectHasTurret(state, *tank)) {
                        tank->turretFacing = runtimeAdvanceFacingClock(tank->turretFacingClock,
                            previousTurret >= 0 ? previousTurret
                                : (previousBody >= 0 ? previousBody : desiredFacing),
                            desiredFacing, runtimeTurretTurnStep(state, *tank),
                            state.tick, state.config.fixedTickHz);
                        turretReady = tank->turretFacing == desiredFacing;
                        if (tank->turretFacing != previousTurret)
                            ++state.actionSummary.turretTurnsThisTick;
                        if (tank->turretFacingClock.active())
                            markRuntimeTurretSoundRotation(state, *tank);
                    }
                    changed = changed || previousBody != tank->facing
                        || previousTurret != tank->turretFacing;
                    if (tank->facing == desiredFacing && turretReady) {
                        host.tankBunkerAnimationStartTick = state.tick;
                        emitRuntimeSound(state, "sound:" + runtimeRulesSoundSection(
                            state, "BunkerWallsUpSound", "TankBunkerUp"),
                            runtimeSpatialAudioPositionForObject(host));
                        changed = true;
                    }
                }
            }
        }
        if (host.tankBunkerLowering) {
            const auto* art = runtimeArtTypeForObject(state, host);
            int duration = 0;
            if (art != nullptr) for (const auto& layer : art->specialAnimations) {
                if (!runtimeTankBunkerLayerVisible(host, layer.key)) continue;
                if (const auto* animation = state.art.findArtType(layer.image)) {
                    duration = std::max(duration, runtimeTankBunkerFrameCount(*animation)
                        * runtimeTankBunkerFrameDelay(*animation, state.config.fixedTickHz));
                }
            }
            if (state.tick - host.tankBunkerAnimationStartTick >= duration) {
                host.tankBunkerLowering = false;
                host.tankBunkerAnimationStartTick = -1;
            }
            changed = true;
        }
    }
    return changed;
}

bool runtimeObjectCanEnterTransport(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport,
    bool includeReservations,
    bool requireBoardingPose) {
    if (runtimeInfantryRequiresManualUndeploy(state, passenger)) return false;
    if (runtimeTankBunkerType(state, transport) != nullptr) {
        return runtimeObjectCanEnterTankBunker(state, passenger, transport, includeReservations);
    }
    const BuildingType* absorber = runtimeInfantryAbsorberType(state, transport);
    if (passenger.tankBunkerHostId >= 0 || !passenger.alive || passenger.health <= 0
        || !runtimeTransportPassengerTypeAllowed(state, passenger, transport)
        || passenger.dockedToObjectId >= 0
        || passenger.dockIndex >= 0
        || (passenger.airborne && absorber == nullptr)
        || (transport.kind != WorldObjectKind::Unit && absorber == nullptr)
        || !transport.alive || transport.health <= 0
        || (absorber != nullptr
            && (transport.rubble || transport.buildAnimTicksRemaining > 0))
        || runtimeObjectSuspendedByMagBeam(state, passenger.instanceId)
        || runtimeObjectDisabledByParasite(state, passenger.instanceId)
        || runtimeObjectDisabledByParasite(state, transport.instanceId)
        || passenger.instanceId == transport.instanceId
        || !runtimeOwnersAreAllied(state, passenger.owner, transport.owner)) {
        return false;
    }

    const RuntimeTransportProfile profile =
        runtimeTransportProfileForObject(state, transport);
    if (!profile.canTransport || profile.passengerCapacity <= 0) {
        return false;
    }
    const auto* flightType = runtimeJumpjetProfile(state, transport);
    if (requireBoardingPose && flightType
        && runtimeUnitUsesAutomaticJumpjetLanding(*flightType)
        && (transport.airborne || transport.renderMoveActive)) {
        return false;
    }

    if (runtimeTransportContainsCargo(state, passenger, transport.instanceId)) return false;
    int occupied = runtimeTransportUsedSpace(state, transport);
    if (includeReservations) {
        occupied += runtimeTransportReservedPassengerCount(
            state,
            transport.instanceId,
            passenger.instanceId
        );
    }
    return runtimeTransportPassengerSpace(state, transport, passenger) <= profile.passengerCapacity - occupied;
}

void refreshRuntimeTransportGunnerState(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& transport) {
    transport.activeGunnerMode = -1;
    transport.activeGunnerWeaponIndex = -1;
    transport.activeGunnerTurretIndex = -1;

    const UnitType* transportType =
        findRuntimeUnitType(state.rules, transport);
    if (transportType == nullptr
        || !transportType->gunner
        || transport.transportPassengerIds.empty()) {
        return;
    }

    const WorldObjectInstance* passenger =
        findRuntimeObjectByIdIncludingInactive(
        state,
        transport.transportPassengerIds.front()
    );
    const UnitType* passengerType = passenger == nullptr
        ? nullptr
        : findRuntimeUnitType(state.rules, *passenger);
    if (passengerType == nullptr) {
        return;
    }

    const int mode = std::clamp(
        passengerType->ifvMode < 0 ? 0 : passengerType->ifvMode,
        0,
        static_cast<int>(transportType->ifvWeaponIndices.size()) - 1
    );
    transport.activeGunnerMode = mode;
    transport.activeGunnerWeaponIndex =
        transportType->ifvWeaponIndices[static_cast<std::size_t>(mode)];
    transport.activeGunnerTurretIndex =
        transportType->ifvTurretIndices[static_cast<std::size_t>(mode)];

    // Mods commonly omit the redundant *TurretWeapon mapping while keeping
    // the stock IFVMode -> WeaponN ordering.
    if (transport.activeGunnerWeaponIndex < 0) {
        transport.activeGunnerWeaponIndex = mode;
    }
    if (transport.activeGunnerTurretIndex < 0) {
        transport.activeGunnerTurretIndex = 0;
    }
}

void destroyRuntimeTransportPassengers(
    RuntimeProductionHostState& state,
    WorldObjectInstance& transport,
    const std::string& mission) {
    for (int passengerId : transport.transportPassengerIds) {
        WorldObjectInstance* passenger =
            findMutableRuntimeObjectByIdIncludingInactive(
                state,
                passengerId
            );
        if (passenger == nullptr) {
            continue;
        }
        cancelRuntimeCombatFire(state, passengerId);
        passenger->health = 0;
        passenger->alive = false;
        passenger->mission = mission;
        passenger->subCell = -1;
        clearRuntimeRenderPosition(*passenger);
        removeSelectedObject(state, passengerId);
        eraseRuntimeBehavior(state, passengerId);
    }
    if (!transport.transportPassengerIds.empty()
        && runtimeInfantryAbsorberType(state, transport) != nullptr)
        runtimeRestartBioReactorAnimation(transport, state.tick);
    transport.transportPassengerIds.clear();
    refreshRuntimeTransportGunnerState(state, transport);

    std::vector<int> cancelledPassengerIds;
    for (const auto& [passengerId, targetId] :
            state.pendingTransportTargets) {
        if (targetId == transport.instanceId) {
            cancelledPassengerIds.push_back(passengerId);
        }
    }
    for (int passengerId : cancelledPassengerIds) {
        if (WorldObjectInstance* passenger =
                findMutableObjectById(state, passengerId)) {
            setRuntimeMissionIntent(
                state,
                *passenger,
                RuntimeMissionIntent::Idle,
                "transportDestroyed"
            );
        }
        eraseRuntimeBehavior(state, passengerId);
    }
}

bool runtimePersonalChronoTransportPassenger(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger) {
    const auto* type = findRuntimeUnitType(state.rules, passenger);
    return passenger.kind == WorldObjectKind::Infantry && type
        && type->teleporter && !type->harvester;
}

std::optional<bool> runtimeChronoTransportDestinationAdmission(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const RuntimeCellPoint& destination) {
    if (!runtimePersonalChronoTransportPassenger(state, passenger)) return std::nullopt;
    const auto pending = state.pendingTransportTargets.find(passenger.instanceId);
    if (pending == state.pendingTransportTargets.end()) return std::nullopt;
    const auto* host = findRuntimeObjectByIdIncludingInactive(state, pending->second);
    if (!host || host->kind != WorldObjectKind::Unit) return std::nullopt;
    const auto move = state.behaviors.find(passenger.instanceId);
    if (move == state.behaviors.end() || move->second.kind != RuntimeBehaviorKind::Move
        || move->second.pendingAllowedOccupiedObjectId != host->instanceId
        || move->second.moveTarget != destination || runtimeCellPointForObject(*host) != destination
        || host->airborne || host->renderMoveActive
        || !runtimeObjectCanEnterTransport(state, passenger, *host, true, true)) return false;
    const auto* warp = state.specialMechanisms.chronoMoveState(passenger.instanceId);
    if (warp && state.specialMechanisms.isChronoInTransit(passenger.instanceId)
        && (warp->destination.x != destination.x || warp->destination.y != destination.y
            || warp->destinationLevel != host->level)) return false;
    return true;
}

bool runtimeChronoTransportArrivalReady(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const WorldObjectInstance& host) {
    const auto* warp = state.specialMechanisms.chronoMoveState(passenger.instanceId);
    const auto admitted = runtimeChronoTransportDestinationAdmission(
        state, passenger, runtimeCellPointForObject(host));
    return admitted && *admitted && warp
        && (warp->phase == ChronoPhase::WarpIn || warp->phase == ChronoPhase::Cooldown)
        && warp->completeTick <= state.tick
        && warp->destination.x == host.x && warp->destination.y == host.y
        && warp->destinationLevel == host.level
        && runtimeCellPointForObject(passenger) == runtimeCellPointForObject(host)
        && passenger.level == host.level && !passenger.renderMoveActive;
}

bool completeRuntimeTransportEntry(
    RuntimeProductionHostState& state,
    int passengerId,
    bool initialCargo) {
    const auto pendingIt = state.pendingTransportTargets.find(passengerId);
    if (pendingIt == state.pendingTransportTargets.end()) {
        return false;
    }

    WorldObjectInstance* passenger =
        findMutableObjectById(state, passengerId);
    WorldObjectInstance* transport =
        findMutableObjectById(state, pendingIt->second);
    if (transport != nullptr && runtimeTankBunkerType(state, *transport) != nullptr) {
        return completeRuntimeTankBunkerEntry(state, passengerId, transport->instanceId);
    }
    if (passenger == nullptr
        || transport == nullptr
        || !runtimeObjectCanEnterTransport(
            state,
            *passenger,
            *transport,
            false
        )) {
        state.pendingTransportTargets.erase(pendingIt);
        return false;
    }

    const RuntimeCellPoint passengerCell =
        runtimeCellPointForObject(*passenger);
    const RuntimeCellPoint transportCell =
        runtimeCellPointForObject(*transport);
    const bool bioReactor = runtimeInfantryAbsorberType(state, *transport) != nullptr;
    if (initialCargo && (transport->kind != WorldObjectKind::Unit
        || passengerCell != transportCell || passenger->subCell != -1
        || passenger->renderMoveActive || state.behaviors.contains(passengerId))) return false;
    // Buildings require arrival inside their foundation, not proximity to its origin.
    if (bioReactor
            ? runtimeCellDistanceToObjectFootprint(state, passengerCell, *transport) > 0.0
            : runtimeCellDistance(passengerCell, transportCell) > 1.5) {
        return false;
    }

    if (passenger->renderMoveActive) return false;
    if (!initialCargo && transport->kind == WorldObjectKind::Unit
        && !advanceRuntimeUnitTransportBoarding(state, *passenger, *transport)) return false;
    const std::string passengerTypeId = passenger->type;
    const std::string transportTypeId = transport->type;
    const UnitType* transportType =
        findRuntimeUnitType(state.rules, *transport);
    cancelRuntimeCombatFire(state, passengerId);
    transport->transportPassengerIds.push_back(passengerId);
    refreshRuntimeTransportGunnerState(state, *transport);

    passenger->x = transport->x;
    passenger->y = transport->y;
    passenger->cell = transport->cell;
    passenger->level = transport->level;
    passenger->onBridge = transport->onBridge;
    releaseRuntimeInfantrySubcellReservation(state, passengerId);
    passenger->subCell = -1;
    passenger->alive = false;
    passenger->mission = "InsideTransport";
    passenger->airborne = false;
    state.jumpjetFlights.erase(passengerId);
    state.aircraftFlights.erase(passengerId);
    passenger->flightLevel = passenger->level;
    clearRuntimeRenderPosition(*passenger);
    syncRuntimeTransportCargoPositions(state, *passenger);
    if (auto task = state.harvesterTasks.find(passengerId); task != state.harvesterTasks.end()) {
        // Interrupt work/docking while retaining the ore already inside the vehicle.
        task->second.phase = RuntimeHarvesterPhase::Idle;
        task->second.targetOre = task->second.dockCell = task->second.queueCell = {-1, -1};
        task->second.targetRefineryId = -1;
        task->second.workTicksRemaining = 0;
        task->second.nextThinkTick = state.tick + 1;
        task->second.status = "inside transport";
    }
    state.pendingTransportTargets.erase(pendingIt);
    removeSelectedObject(state, passengerId);

    state.interactionStatus =
        passengerTypeId
        + " entered "
        + transportTypeId
        + " #"
        + std::to_string(transport->instanceId)
        + " mode="
        + std::to_string(transport->activeGunnerMode);
    state.economy.status = state.interactionStatus;
    if (bioReactor) {
        runtimeRestartBioReactorAnimation(*transport, state.tick);
        rebuildRuntimeEconomyPower(state);
        emitRuntimeSound(state, "sound:" + runtimeRulesSoundSection(
            state, "EnterBioReactorSound", "BioReactorEnter"));
    }
    if (transportType != nullptr
        && !trimAsciiLocal(transportType->enterTransportSound).empty()) {
        emitRuntimeSound(
            state,
            "sound:" + trimAsciiLocal(transportType->enterTransportSound)
        );
    }
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    refreshRuntimeActionSummary(state);
    return true;
}

const BuildingType* runtimeInfantryServiceBuildingRules(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target) {
    if (target.kind != WorldObjectKind::Structure) {
        return nullptr;
    }
    const BuildingType* building = findRuntimeBuildingTypeById(
        state.rules,
        target.type
    );
    return building != nullptr && (building->hospital || building->armory)
        ? building
        : nullptr;
}

bool runtimeInfantryServiceOwnersMatch(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& infantry,
    const WorldObjectInstance& target) {
    return runtimeRepairDepotCanonicalOwnerId(state, infantry.owner)
        == runtimeRepairDepotCanonicalOwnerId(state, target.owner);
}

RuntimeInfantryServiceEvaluation runtimeInfantryServiceEvaluationFor(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& infantry,
    const WorldObjectInstance& target) {
    return evaluateRuntimeInfantryService(
        infantry,
        target,
        runtimeInfantryServiceBuildingRules(state, target),
        runtimeMaxHealthForObject(state, infantry),
        runtimeInfantryServiceOwnersMatch(state, infantry, target)
    );
}

bool runtimeBehaviorIsPendingInfantryService(
    const RuntimeGameSessionState& state,
    int objectId,
    const RuntimeObjectBehavior& behavior) {
    if (behavior.kind != RuntimeBehaviorKind::Move
        || behavior.targetObjectId < 0
        || behavior.pendingAllowedOccupiedObjectId
            != behavior.targetObjectId) {
        return false;
    }
    const WorldObjectInstance* infantry = findObjectById(state, objectId);
    const WorldObjectInstance* target = findObjectById(
        state,
        behavior.targetObjectId
    );
    return infantry != nullptr
        && infantry->kind == WorldObjectKind::Infantry
        && target != nullptr
        && runtimeInfantryServiceBuildingRules(state, *target) != nullptr;
}

bool runtimeSelectionHasInfantryServiceRelationship(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& target) {
    if (runtimeInfantryServiceBuildingRules(state, target) == nullptr) {
        return false;
    }
    for (int objectId : state.selectedObjectIds) {
        const WorldObjectInstance* infantry = findObjectById(state, objectId);
        if (infantry != nullptr
            && infantry->alive
            && infantry->kind == WorldObjectKind::Infantry
            && infantry->instanceId != target.instanceId
            && !runtimeObjectDisabledByParasite(state, objectId)
            && runtimeInfantryServiceOwnersMatch(
                state,
                *infantry,
                target
            )) {
            return true;
        }
    }
    return false;
}

bool queueRuntimeInfantryServiceTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetStructure) {
    if (runtimeInfantryServiceBuildingRules(
            state,
            targetStructure
        ) == nullptr) {
        return false;
    }

    std::vector<int> selectedIds = state.selectedObjectIds;
    std::sort(selectedIds.begin(), selectedIds.end());
    selectedIds.erase(
        std::unique(selectedIds.begin(), selectedIds.end()),
        selectedIds.end()
    );
    int queuedCount = 0;
    for (int objectId : selectedIds) {
        WorldObjectInstance* infantry = findMutableObjectById(state, objectId);
        WorldObjectInstance* target = findMutableObjectById(
            state,
            targetStructure.instanceId
        );
        if (infantry == nullptr || target == nullptr
            || runtimeObjectDisabledByParasite(state, objectId)
            || runtimeInfantryRequiresManualUndeploy(state, *infantry)
            || !runtimeInfantryServiceEvaluationFor(
                state,
                *infantry,
                *target
            ).usable) {
            continue;
        }
        const std::optional<RuntimeCellPoint> entryCell =
            findRuntimeEngineerEntryCell(
                state,
                *infantry,
                *target,
                "infantry-service-enter",
                2
            );
        if (!entryCell.has_value()
            || !releaseRuntimeRepairDepotForCommand(
                state,
                infantry->instanceId,
                "infantry service command"
            )) {
            continue;
        }

        if (!eraseRuntimeBehavior(state, infantry->instanceId)) continue;
        RuntimeObjectBehavior behavior;
        behavior.kind = RuntimeBehaviorKind::Move;
        behavior.state = RuntimeBehaviorState::Queued;
        behavior.targetObjectId = target->instanceId;
        behavior.moveTarget = *entryCell;
        behavior.pendingAllowedOccupiedObjectId = target->instanceId;
        setRuntimeMissionIntent(
            state,
            *infantry,
            RuntimeMissionIntent::Entering,
            "infantryServiceQueued"
        );
        state.behaviors[infantry->instanceId] = std::move(behavior);
        ++queuedCount;
    }

    if (queuedCount <= 0) {
        return false;
    }
    state.interactionStatus = "entering " + targetStructure.type
        + " #" + std::to_string(targetStructure.instanceId)
        + " infantry=" + std::to_string(queuedCount);
    state.economy.status = state.interactionStatus;
    if (!state.runtimeSuppressPlayerAudioFeedback) {
        emitRuntimeSound(
            state,
            runtimeVoiceEventForSelectedUnits(state, "enter")
                .value_or("command target")
        );
    }
    refreshRuntimeActionSummary(state);
    return true;
}

bool consumeRuntimeBlockedInfantryServiceTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetStructure) {
    if (!runtimeSelectionHasInfantryServiceRelationship(
            state,
            targetStructure
        )) {
        return false;
    }
    state.interactionStatus = targetStructure.type + " #"
        + std::to_string(targetStructure.instanceId)
        + " cannot service selected infantry";
    state.economy.status = state.interactionStatus;
    if (!state.runtimeSuppressPlayerAudioFeedback) {
        emitRuntimeSound(state, "blocked");
    }
    refreshRuntimeActionSummary(state);
    return true;
}

bool completeRuntimeInfantryService(
    RuntimeProductionHostState& state,
    int infantryObjectId) {
    const auto behavior = state.behaviors.find(infantryObjectId);
    if (behavior == state.behaviors.end()
        || !runtimeBehaviorIsPendingInfantryService(
            state,
            infantryObjectId,
            behavior->second
        )) {
        return false;
    }
    WorldObjectInstance* infantry = findMutableObjectById(
        state,
        infantryObjectId
    );
    WorldObjectInstance* target = findMutableObjectById(
        state,
        behavior->second.targetObjectId
    );
    if (infantry == nullptr || target == nullptr
        || runtimeCellDistanceToObjectFootprint(
            state,
            runtimeCellPointForObject(*infantry),
            *target
        ) > 0.001) {
        return false;
    }

    // Original RA2 places the returning soldier at the upper-left side of
    // the service building. Use that authored direction, then let the normal
    // deterministic passability search select the nearest legal subcell.
    const std::optional<RuntimeCellPoint> exitCell =
        findRuntimePassableCellNear(
            state,
            *infantry,
            {target->x - 1, target->y - 1},
            4
        );
    if (!exitCell.has_value()) {
        state.simulationStatus = "infantry service exit blocked #"
            + std::to_string(infantryObjectId);
        return false;
    }

    const BuildingType* building = runtimeInfantryServiceBuildingRules(
        state,
        *target
    );
    const RuntimeInfantryServiceResult result = applyRuntimeInfantryService(
        *infantry,
        *target,
        building,
        runtimeMaxHealthForObject(state, *infantry),
        runtimeInfantryServiceOwnersMatch(state, *infantry, *target)
    );
    const int targetId = target->instanceId;
    const std::string targetType = target->type;

    releaseRuntimeInfantrySubcellReservation(state, infantryObjectId);
    setRuntimeObjectCell(state, *infantry, *exitCell);
    setRuntimeMissionIntent(
        state,
        *infantry,
        RuntimeMissionIntent::Idle,
        result.accepted
            ? "infantryServiceComplete"
            : "infantryServiceNoEffect"
    );
    clearRuntimeRenderPosition(*infantry);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    state.repaintRequested = true;
    state.interactionStatus = result.accepted
        ? targetType + " serviced infantry #"
            + std::to_string(infantryObjectId)
            + " charges=" + std::to_string(result.ammoAfter)
        : targetType + " released infantry #"
            + std::to_string(infantryObjectId)
            + " without service ("
            + runtimeInfantryServiceVetoName(result.veto) + ")";
    state.economy.status = state.interactionStatus;
    state.simulationStatus = state.interactionStatus;
    invalidateRuntimeObjectProjectionCache(state);
    (void)targetId;
    refreshRuntimeActionSummary(state);
    return true;
}

std::optional<RuntimeCellPoint> runtimeTransportEntryCell(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport) {
    if (runtimeTankBunkerType(state, transport) != nullptr) {
        return runtimeTankBunkerEntryCell(state, passenger, transport);
    }
    if (runtimeInfantryAbsorberType(state, transport) != nullptr) {
        return findRuntimeEngineerEntryCell(
            state, passenger, transport, "bioreactor-entry", 2);
    }
    if (transport.kind == WorldObjectKind::Unit) {
        if (runtimePersonalChronoTransportPassenger(state, passenger)) {
            if (transport.airborne || transport.renderMoveActive
                || !runtimeObjectCanEnterTransport(state, passenger, transport, true, true)) return std::nullopt;
            return runtimeCellPointForObject(transport);
        }
        return runtimeAmphibiousTransportAccessCell(state, passenger, transport, true);
    }
    return findRuntimePassableCellNear(
        state, passenger, runtimeCellPointForObject(transport),
        2, transport.instanceId);
}

bool queueRuntimeTransportTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetTransport) {
    if ((targetTransport.kind != WorldObjectKind::Unit
            && runtimeTankBunkerType(state, targetTransport) == nullptr
            && runtimeInfantryAbsorberType(state, targetTransport) == nullptr)
        || !targetTransport.alive) {
        return false;
    }

    WorldObjectInstance* transport =
        findMutableObjectById(state, targetTransport.instanceId);
    if (transport == nullptr) {
        return false;
    }
    const RuntimeTransportProfile transportProfile =
        runtimeTransportProfileForObject(state, *transport);
    if (!transportProfile.canTransport
        || transportProfile.passengerCapacity <= 0) {
        return false;
    }

    if (transport->kind == WorldObjectKind::Unit)
        resetLastRuntimeCommand(state, "enter", runtimeCellPointForObject(*transport),
            transport->instanceId, transport->type);
    int queuedCount = 0;
    bool selectedAlliedPassenger = false;
    auto selectedIds = state.selectedObjectIds;
    std::sort(selectedIds.begin(), selectedIds.end());
    selectedIds.erase(std::unique(selectedIds.begin(), selectedIds.end()), selectedIds.end());
    for (int passengerId : selectedIds) {
        WorldObjectInstance* passenger =
            findMutableObjectById(state, passengerId);
        if (passenger != nullptr
            && (passenger->kind == WorldObjectKind::Infantry || passenger->kind == WorldObjectKind::Unit)
            && runtimeOwnersAreAllied(
                state,
                passenger->owner,
                transport->owner
            )) {
            selectedAlliedPassenger = true;
        }
        if (passenger == nullptr
            || !runtimeObjectCanEnterTransport(
                state,
                *passenger,
                *transport,
                true
            )) {
            continue;
        }

        if (transport->kind == WorldObjectKind::Unit
            && runtimePersonalChronoTransportPassenger(state, *passenger)
            && state.specialMechanisms.isChronoMovementLocked(passengerId, state.tick)) continue;

        const std::optional<RuntimeCellPoint> entryCell =
            runtimeTransportEntryCell(state, *passenger, *transport);
        if (!entryCell.has_value()) {
            continue;
        }
        const RuntimeCellPoint passengerCell =
            runtimeCellPointForObject(*passenger);
        const bool personalChrono = transport->kind == WorldObjectKind::Unit
            && runtimePersonalChronoTransportPassenger(state, *passenger);
        const std::vector<RuntimeCellPoint> entryPath = personalChrono
            ? std::vector<RuntimeCellPoint>{}
            : findRuntimePath(state, *passenger, passengerCell, *entryCell, transport->instanceId);
        if (!personalChrono && passengerCell != *entryCell && entryPath.empty()) {
            continue;
        }

        const auto previous = state.behaviors.find(passengerId);
        const std::optional<RuntimeObjectBehavior> accepted = previous != state.behaviors.end()
            && (previous->second.kind == RuntimeBehaviorKind::Move
                || previous->second.kind == RuntimeBehaviorKind::Attack)
            && passenger->renderMoveActive && previous->second.moveCooldownTicks > 0
            ? std::optional<RuntimeObjectBehavior>(previous->second) : std::nullopt;
        const auto oldBody = runtimeTransportMotionBody(*passenger);
        const bool hadPose = passenger->hasRenderPosition;
        const bool cancellingStructureEntry = runtimeCancelledStructureEntryNeedsRecovery(state, *passenger);
        if (!eraseRuntimeBehavior(state, passengerId)) continue;
        RuntimeObjectBehavior behavior;
        behavior.kind = RuntimeBehaviorKind::Move;
        behavior.state = RuntimeBehaviorState::Queued;
        behavior.moveTarget = *entryCell;
        behavior.pendingAllowedOccupiedObjectId = transport->instanceId;
        replaceRuntimeBehaviorPath(behavior, cancellingStructureEntry ? std::vector<RuntimeCellPoint>{} : entryPath);
        if (accepted && !cancellingStructureEntry && !personalChrono) inheritRuntimeVehicleMoveSegment(*passenger, behavior, *accepted);
        else if (hadPose && !cancellingStructureEntry) restoreRuntimeTransportMotionBody(*passenger, oldBody);
        setRuntimeMissionIntent(
            state,
            *passenger,
            RuntimeMissionIntent::Entering,
            "transportEnter"
        );
        state.pendingTransportTargets[passengerId] =
            transport->instanceId;
        state.behaviors[passengerId] = std::move(behavior);
        ++queuedCount;
    }
    if (transport->kind == WorldObjectKind::Unit) {
        state.lastCommand.acceptedCount = queuedCount;
        state.lastCommand.rejectedCount = std::max(0, state.lastCommand.selectedCount - queuedCount);
    }

    if (queuedCount <= 0) {
        if (!selectedAlliedPassenger) {
            return false;
        }
        state.interactionStatus =
            transport->type
            + " #"
            + std::to_string(transport->instanceId)
            + " cannot accept selected passengers";
        state.economy.status = state.interactionStatus;
        if (!state.runtimeSuppressPlayerAudioFeedback) {
            emitRuntimeSound(state, "blocked");
        }
        refreshRuntimeActionSummary(state);
        return true;
    }

    state.interactionStatus =
        "boarding "
        + transport->type
        + " #"
        + std::to_string(transport->instanceId)
        + " passengers="
        + std::to_string(queuedCount);
    state.economy.status = state.interactionStatus;
    if (!state.runtimeSuppressPlayerAudioFeedback) {
        emitRuntimeSound(
            state,
            runtimeVoiceEventForSelectedUnits(state, "enter")
                .value_or("command target")
        );
    }
    refreshRuntimeActionSummary(state);
    return true;
}

int unloadRuntimeTransport(
    RuntimeProductionHostState& state,
    WorldObjectInstance& transport) {
    if (transport.transportPassengerIds.empty()) {
        refreshRuntimeTransportGunnerState(state, transport);
        return 0;
    }

    if (transport.kind == WorldObjectKind::Unit) return unloadRuntimeUnitTransportMotion(state, transport);

    std::vector<int> remainingPassengerIds;
    remainingPassengerIds.reserve(transport.transportPassengerIds.size());
    std::unordered_set<int> reservedCells;
    std::unordered_set<int> dischargedCells;
    const bool amphibious = runtimeAmphibiousTransport(state, transport);
    int unloadedCount = 0;
    // Publish disembarkation before querying the next passenger's ground
    // slots: the subcell system excludes every id still in a manifest.
    const auto passengerIds = transport.transportPassengerIds;
    transport.transportPassengerIds.clear();
    for (int passengerId : passengerIds) {
        WorldObjectInstance* passenger =
            findMutableRuntimeObjectByIdIncludingInactive(
                state,
                passengerId
            );
        if (passenger == nullptr) {
            continue;
        }

        const std::optional<RuntimeCellPoint> exitCell = amphibious
            ? runtimeAmphibiousTransportAccessCell(state, *passenger, transport, false,
                passenger->kind == WorldObjectKind::Infantry ? &reservedCells : &dischargedCells)
            :
            findRuntimePassableCellNear(
                state,
                *passenger,
                runtimeCellPointForObject(transport),
                2,
                -1,
                &reservedCells
            );
        if (!exitCell.has_value()) {
            remainingPassengerIds.push_back(passengerId);
            continue;
        }

        cancelRuntimeCombatFire(state, passengerId);
        eraseRuntimeBehavior(state, passengerId);
        passenger->x = exitCell->x;
        passenger->y = exitCell->y;
        passenger->cell = runtimeCellKey(exitCell->x, exitCell->y);
        passenger->level = terrainLevelAtRuntimeCell(
            state,
            exitCell->x,
            exitCell->y
        );
        passenger->onBridge = runtimeCellIsBridge(
            state,
            exitCell->x,
            exitCell->y
        );
        activateRuntimeWorldObject(state, *passenger);
        passenger->facing = transport.facing;
        passenger->turretFacing = -1;
        passenger->mission = runtimeMissionName(RuntimeMissionIntent::Idle);
        passenger->hasRenderPosition = false;
        passenger->renderMoveActive = false;
        passenger->renderX = static_cast<double>(passenger->x);
        passenger->renderY = static_cast<double>(passenger->y);
        passenger->renderLevel = static_cast<double>(passenger->level);
        passenger->renderOnBridge = passenger->onBridge;
        if (runtimeInfantryAbsorberType(state, transport) != nullptr) {
            departRuntimeAircraftDock(state, *passenger);
            clearRuntimeRenderPosition(*passenger);
        }
        assignRuntimeInfantrySubcellAtCurrentLocation(state, *passenger);
        syncRuntimeTransportCargoPositions(state, *passenger);
        const int exitKey = runtimeCellKey(exitCell->x, exitCell->y);
        dischargedCells.insert(exitKey);
        if (!amphibious || passenger->kind != WorldObjectKind::Infantry) reservedCells.insert(exitKey);
        ++unloadedCount;
    }

    transport.transportPassengerIds = std::move(remainingPassengerIds);
    refreshRuntimeTransportGunnerState(state, transport);
    setRuntimeMissionIntent(
        state,
        transport,
        RuntimeMissionIntent::Idle,
        unloadedCount > 0
            ? "transportUnloadComplete"
            : "transportUnloadBlocked"
    );
    if (unloadedCount > 0) {
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        if (runtimeInfantryAbsorberType(state, transport) != nullptr) {
            runtimeRestartBioReactorAnimation(transport, state.tick);
            rebuildRuntimeEconomyPower(state);
            emitRuntimeSound(state, "sound:" + runtimeRulesSoundSection(
                state, "LeaveBioReactorSound", "BioReactorEnter"));
        }
    }
    return unloadedCount;
}

void ensureRuntimeRepairDepotsInitialized(RuntimeGameSessionState& state) {
    if (state.repairDepotsInitialized) {
        return;
    }
    state.repairDepots.reset(std::max(1, state.config.fixedTickHz));
    state.repairDepotsInitialized = true;
}

std::string runtimeRepairDepotCanonicalOwnerId(
    const RuntimeGameSessionState& state,
    const std::string& owner) {
    std::string key = toLowerAsciiLocal(trimAsciiLocal(owner));
    if (state.campaignMode) {
        const auto alias =
            state.triggers.campaignCanonicalHouseByAlias.find(key);
        if (alias != state.triggers.campaignCanonicalHouseByAlias.end()) {
            key = toLowerAsciiLocal(trimAsciiLocal(alias->second));
        }
        const auto house =
            state.triggers.campaignHouseProduction.find(key);
        if (house != state.triggers.campaignHouseProduction.end()
            && house->second.playerControlled) {
            std::string localKey = toLowerAsciiLocal(
                trimAsciiLocal(state.economy.owner)
            );
            const auto localAlias =
                state.triggers.campaignCanonicalHouseByAlias.find(localKey);
            if (localAlias
                != state.triggers.campaignCanonicalHouseByAlias.end()) {
                localKey = toLowerAsciiLocal(
                    trimAsciiLocal(localAlias->second)
                );
            }
            if (!localKey.empty()) {
                key = localKey;
            }
        }
    }
    return key;
}

int runtimeRepairDepotCreditBalance(
    const RuntimeProductionHostState& state,
    const std::string& canonicalOwner) {
    const std::string ownerKey =
        runtimeRepairDepotCanonicalOwnerId(state, canonicalOwner);
    if (ownerKey.empty()) {
        return 0;
    }
    if (ownerKey == runtimeRepairDepotCanonicalOwnerId(
            state,
            state.economy.owner
        )) {
        return std::max(0, state.economy.credits);
    }
    if (const auto* humanEconomy = runtimeHumanHouseEconomy(state, ownerKey)) {
        return std::max(0, humanEconomy->credits);
    }
    if (state.campaignMode) {
        const auto house =
            state.triggers.campaignHouseProduction.find(ownerKey);
        if (house != state.triggers.campaignHouseProduction.end()) {
            return house->second.playerControlled
                ? std::max(0, state.economy.credits)
                : std::max(0, house->second.credits);
        }
    }
    const auto ai = state.computerAiByOwner.find(ownerKey);
    return ai == state.computerAiByOwner.end()
        ? 0
        : std::max(0, ai->second.credits);
}

bool runtimeRepairDepotCellIsOpenWater(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell) {
    const TerrainCell* terrain = findRuntimeTerrainCellAt(state, cell);
    if (terrain == nullptr) {
        return false;
    }
    const RuntimeTerrainInfo info =
        runtimeCachedTerrainInfoForCell(state, *terrain);
    return info.passable
        && info.water
        && !info.bridge
        && !runtimeCellIsBridge(state, cell.x, cell.y);
}

std::optional<RuntimeRepairDepotCell> runtimeRepairDepotRawDockCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& host,
    int dockIndex) {
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, host.type);
    if (!host.alive
        || host.health <= 0
        || host.kind != WorldObjectKind::Structure
        || building == nullptr
        || !building->unitRepair
        || dockIndex < 0
        || dockIndex >= building->numberOfDocks) {
        return std::nullopt;
    }
    const RuntimeProductionSpawn dock =
        runtimeAircraftDockSpawn(state, host, dockIndex);
    if (dock.cell.x < 0 || dock.cell.y < 0) {
        return std::nullopt;
    }
    if (building->naval || building->waterBound) {
        const ArtType* art = runtimeArtTypeForObject(state, host);
        if (art == nullptr) {
            return std::nullopt;
        }
        const auto authoredOffset = std::find_if(
            art->dockingOffsets.begin(),
            art->dockingOffsets.end(),
            [dockIndex](const ArtType::DockingOffset& candidate) {
                return candidate.index == dockIndex;
            }
        );
        if (authoredOffset == art->dockingOffsets.end()) {
            return std::nullopt;
        }
        // The shared docking coordinates retain the half-cell art offset.
        // Validate the containing navigation cell, as runtimeAircraftDockSpawn does.
        const RuntimeCellPoint authoredCell{
            static_cast<int>(std::floor(dock.renderX)),
            static_cast<int>(std::floor(dock.renderY))
        };
        if (dock.cell != authoredCell
            || !runtimeRepairDepotCellIsOpenWater(state, authoredCell)) {
            return std::nullopt;
        }
        return RuntimeRepairDepotCell{
            authoredCell.x,
            authoredCell.y,
            false
        };
    }
    return RuntimeRepairDepotCell{
        dock.cell.x,
        dock.cell.y,
        host.onBridge
    };
}

std::optional<RuntimeRepairDepotCell> runtimeRepairDepotDockCell(
    const RuntimeGameSessionState& state,
    int hostId,
    int dockIndex,
    int unitId) {
    const WorldObjectInstance* host = findObjectById(state, hostId);
    if (host == nullptr) {
        return std::nullopt;
    }
    const std::optional<RuntimeRepairDepotCell> candidate =
        runtimeRepairDepotRawDockCell(state, *host, dockIndex);
    if (!candidate.has_value()) {
        return std::nullopt;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, host->type);
    const WorldObjectInstance* unit = unitId >= 0
        ? findObjectById(state, unitId)
        : nullptr;
    if (unitId >= 0 && unit == nullptr) {
        return std::nullopt;
    }
    if (unit != nullptr
        && runtimeObjectMovementMode(state, *unit) != PathMovementMode::Air) {
        const RuntimeCellPoint exact{candidate->x, candidate->y};
        if (!runtimeStructureCellIsVehicleDriveRow(
                state,
                *host,
                exact,
                *unit
            )) {
            return std::nullopt;
        }
        const std::optional<RuntimeCellPoint> passable =
            findRuntimePassableCellNear(
                state,
                *unit,
                exact,
                0,
                hostId
            );
        if (!passable.has_value() || *passable != exact) {
            return std::nullopt;
        }
    }
    if (building != nullptr
        && (building->naval || building->waterBound)
        && unitId >= 0
        && (unit == nullptr
            || runtimeObjectMovementMode(state, *unit)
                != PathMovementMode::Naval)) {
        return std::nullopt;
    }
    // A modded multi-dock depot may omit or duplicate DockingOffset entries.
    // Preserve the lower authored slot and safely reduce capacity instead of
    // allowing two units to occupy the same physical cell.
    for (int earlier = 0; earlier < dockIndex; ++earlier) {
        if (runtimeRepairDepotRawDockCell(state, *host, earlier)
            == candidate) {
            return std::nullopt;
        }
    }
    return candidate;
}

std::optional<RuntimeCellPoint> runtimeRepairDepotPerimeterCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& host,
    int slotIndex) {
    if (slotIndex < 0 || slotIndex >= 64) {
        return std::nullopt;
    }
    const RuntimeCellPoint anchor = runtimeCellPointForObject(host);
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, host);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const RuntimeRepairDepotCell dock =
        runtimeRepairDepotDockCell(state, host.instanceId, 0)
            .value_or(RuntimeRepairDepotCell{
                anchor.x + width / 2,
                anchor.y + height / 2,
                host.onBridge
            });

    std::vector<RuntimeCellPoint> candidates;
    candidates.reserve(64);
    for (int ring = 1; ring <= 16 && candidates.size() < 64U; ++ring) {
        const int left = anchor.x - ring;
        const int right = anchor.x + width - 1 + ring;
        const int top = anchor.y - ring;
        const int bottom = anchor.y + height - 1 + ring;
        std::vector<RuntimeCellPoint> ringCells;
        for (int x = left; x <= right; ++x) {
            ringCells.push_back({x, top});
            if (bottom != top) {
                ringCells.push_back({x, bottom});
            }
        }
        for (int y = top + 1; y < bottom; ++y) {
            ringCells.push_back({left, y});
            if (right != left) {
                ringCells.push_back({right, y});
            }
        }
        std::sort(
            ringCells.begin(),
            ringCells.end(),
            [&dock](const RuntimeCellPoint& leftCell,
                    const RuntimeCellPoint& rightCell) {
                const long long leftX = leftCell.x - dock.x;
                const long long leftY = leftCell.y - dock.y;
                const long long rightX = rightCell.x - dock.x;
                const long long rightY = rightCell.y - dock.y;
                const long long leftDistance =
                    (leftX * leftX) + (leftY * leftY);
                const long long rightDistance =
                    (rightX * rightX) + (rightY * rightY);
                if (leftDistance != rightDistance) {
                    return leftDistance < rightDistance;
                }
                if (leftCell.y != rightCell.y) {
                    return leftCell.y < rightCell.y;
                }
                return leftCell.x < rightCell.x;
            }
        );
        candidates.insert(
            candidates.end(),
            ringCells.begin(),
            ringCells.end()
        );
    }
    if (static_cast<std::size_t>(slotIndex) >= candidates.size()) {
        return std::nullopt;
    }
    return candidates[static_cast<std::size_t>(slotIndex)];
}

std::optional<RuntimeRepairDepotCell> runtimeRepairDepotQueueCell(
    RuntimeGameSessionState& state,
    int hostId,
    int queueIndex,
    int unitId) {
    const WorldObjectInstance* host = findObjectById(state, hostId);
    const WorldObjectInstance* unit = findObjectById(state, unitId);
    if (host == nullptr || unit == nullptr) {
        return std::nullopt;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, host->type);
    const bool navalHost = building != nullptr
        && (building->naval || building->waterBound);
    if (navalHost
        && runtimeObjectMovementMode(state, *unit)
            != PathMovementMode::Naval) {
        return std::nullopt;
    }
    const std::optional<RuntimeCellPoint> candidate =
        runtimeRepairDepotPerimeterCell(state, *host, queueIndex);
    if (!candidate.has_value()) {
        return std::nullopt;
    }
    if (navalHost
        && !runtimeRepairDepotCellIsOpenWater(state, *candidate)) {
        return std::nullopt;
    }
    const std::optional<RuntimeCellPoint> passable =
        findRuntimePassableCellNear(state, *unit, *candidate, 0);
    if (!passable.has_value() || *passable != *candidate) {
        return std::nullopt;
    }
    return RuntimeRepairDepotCell{
        candidate->x,
        candidate->y,
        runtimeCellIsBridge(state, candidate->x, candidate->y)
    };
}

std::optional<RuntimeRepairDepotCell> runtimeRepairDepotRallyCell(
    RuntimeProductionHostState& state,
    int hostId,
    int dockIndex,
    int unitId,
    const RuntimeRepairDepotSystemState& authority,
    const std::set<std::tuple<int, int, bool>>* batchReservedCells) {
    const WorldObjectInstance* host = findObjectById(state, hostId);
    const WorldObjectInstance* unit = findObjectById(state, unitId);
    if (host == nullptr || unit == nullptr || dockIndex < 0) {
        return std::nullopt;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, host->type);
    const bool navalHost = building != nullptr
        && (building->naval || building->waterBound);
    if (navalHost
        && runtimeObjectMovementMode(state, *unit)
            != PathMovementMode::Naval) {
        return std::nullopt;
    }
    std::set<std::tuple<int, int, bool>> occupiedRepairCells;
    for (const RuntimeRepairDepotRelation& relation :
            authority.relations()) {
        if (relation.unitId == unitId) {
            continue;
        }
        const RuntimeRepairDepotCell& cell =
            relation.phase == RuntimeRepairDepotRelationPhase::Queued
                ? relation.queueCell
                : relation.dockCell;
        occupiedRepairCells.insert({cell.x, cell.y, cell.onBridge});
    }
    // Both stock repair depots use the same original FoundationOutside table.
    // Its order starts at the front edge; nearest-cell sorting chose the back.
    // Keep other ground hosts and naval dock selection on their existing path.
    const bool stockGroundDepot = !navalHost
        && (toUpperAsciiLocal(host->type) == "GADEPT" || toUpperAsciiLocal(host->type) == "NADEPT");
    constexpr std::array<RuntimeCellPoint, 16> nativeDepotExitOffsets{{
        {0, 3}, {1, 3}, {2, 3}, {-1, 3}, {3, 3}, {-1, 2}, {3, 2}, {-1, 1},
        {3, 1}, {-1, 0}, {3, 0}, {0, -1}, {1, -1}, {2, -1}, {-1, -1}, {3, -1}
    }};
    const int candidateCount = stockGroundDepot ? int(nativeDepotExitOffsets.size()) : 64;
    for (int candidateOffset = 0; candidateOffset < candidateCount;
         ++candidateOffset) {
        const int candidateIndex =
            (std::max(0, dockIndex) + candidateOffset) % 64;
        const std::optional<RuntimeCellPoint> candidate = stockGroundDepot
            ? std::optional<RuntimeCellPoint>(RuntimeCellPoint{
                host->x + nativeDepotExitOffsets[candidateOffset].x,
                host->y + nativeDepotExitOffsets[candidateOffset].y})
            : runtimeRepairDepotPerimeterCell(state, *host, candidateIndex);
        if (!candidate.has_value()) {
            continue;
        }
        const RuntimeRepairDepotCell cell{
            candidate->x,
            candidate->y,
            runtimeCellIsBridge(state, candidate->x, candidate->y)
        };
        if (navalHost
            && !runtimeRepairDepotCellIsOpenWater(state, *candidate)) {
            continue;
        }
        if (occupiedRepairCells.find({cell.x, cell.y, cell.onBridge})
                != occupiedRepairCells.end()
            || (batchReservedCells != nullptr
                && batchReservedCells->find({cell.x, cell.y, cell.onBridge})
                    != batchReservedCells->end())) {
            continue;
        }
        const std::optional<RuntimeCellPoint> passable =
            findRuntimePassableCellNear(state, *unit, *candidate, 0);
        if (!passable.has_value() || *passable != *candidate) {
            continue;
        }
        const RuntimeCellPoint current = runtimeCellPointForObject(*unit);
        const std::vector<RuntimeCellPoint> rallyPath = findRuntimePath(
            static_cast<const RuntimeProductionHostState&>(state),
            *unit,
            current,
            *candidate,
            hostId
        );
        if (current != *candidate && rallyPath.empty()) {
            continue;
        }
        const bool crossesReservedRepairCell = std::any_of(
            rallyPath.begin(),
            rallyPath.end(),
            [&](const RuntimeCellPoint& pathCell) {
                const std::tuple<int, int, bool> key{
                    pathCell.x,
                    pathCell.y,
                    runtimeCellIsBridge(state, pathCell.x, pathCell.y)
                };
                return occupiedRepairCells.contains(key)
                    || (batchReservedCells != nullptr
                        && batchReservedCells->contains(key));
            }
        );
        if (!crossesReservedRepairCell) {
            return cell;
        }
    }
    return std::nullopt;
}

const RuntimeRepairDepotRelation* findRuntimeRepairDepotRelationForUnit(
    const RuntimeRepairDepotSystemState& authority,
    int unitId) {
    const auto found = std::find_if(
        authority.relations().begin(),
        authority.relations().end(),
        [unitId](const RuntimeRepairDepotRelation& relation) {
            return relation.unitId == unitId;
        }
    );
    return found == authority.relations().end() ? nullptr : &*found;
}

std::optional<int> runtimeRepairDepotAllowedHostForCell(
    const RuntimeGameSessionState& state,
    const RuntimeRepairDepotSystemState& authority,
    int unitId,
    const RuntimeRepairDepotCell& cell) {
    const RuntimeRepairDepotRelation* relation =
        findRuntimeRepairDepotRelationForUnit(authority, unitId);
    if (relation != nullptr
        && relation->phase != RuntimeRepairDepotRelationPhase::Queued
        && relation->dockCell == cell) {
        return relation->hostId;
    }

    const WorldObjectInstance* unit = findObjectById(state, unitId);
    const UnitType* unitRules = unit == nullptr
        ? nullptr
        : state.rules.findUnitType("UnitType", unit->type);
    if (unit == nullptr || unitRules == nullptr) {
        return std::nullopt;
    }
    const bool navalUnit = unitRules->naval || unitRules->waterBound;
    std::vector<int> hostIds;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        const BuildingType* building = object.kind == WorldObjectKind::Structure
            ? findRuntimeBuildingTypeById(state.rules, object.type)
            : nullptr;
        if (object.alive
            && building != nullptr
            && building->unitRepair
            && (building->naval || building->waterBound) == navalUnit) {
            hostIds.push_back(object.instanceId);
        }
    }
    std::sort(hostIds.begin(), hostIds.end());
    for (int hostId : hostIds) {
        const WorldObjectInstance* host = findObjectById(state, hostId);
        const BuildingType* building = host == nullptr
            ? nullptr
            : findRuntimeBuildingTypeById(state.rules, host->type);
        if (host == nullptr || building == nullptr) {
            continue;
        }
        for (int dockIndex = 0;
             dockIndex < building->numberOfDocks;
             ++dockIndex) {
            if (runtimeRepairDepotDockCell(state, hostId, dockIndex)
                    == std::optional<RuntimeRepairDepotCell>(cell)) {
                return hostId;
            }
        }
    }
    return std::nullopt;
}

bool runtimeRepairDepotReservationIsActive(
    const RuntimeGameSessionState& state,
    const RuntimeRepairDepotSystemState& authority,
    int hostId,
    int phaseSlotIndex,
    int unitId) {
    const RuntimeRepairDepotRelation* relation =
        findRuntimeRepairDepotRelationForUnit(authority, unitId);
    if (relation == nullptr || relation->hostId != hostId) {
        return false;
    }
    const bool queued =
        relation->phase == RuntimeRepairDepotRelationPhase::Queued;
    if ((queued ? relation->queueIndex : relation->dockIndex)
        != phaseSlotIndex) {
        return false;
    }
    const RuntimeRepairDepotCell expected =
        queued ? relation->queueCell : relation->dockCell;
    const WorldObjectInstance* unit = findObjectById(state, unitId);
    if (unit == nullptr || !unit->alive) {
        return false;
    }
    if (unit->x == expected.x
        && unit->y == expected.y
        && unit->onBridge == expected.onBridge) {
        return true;
    }
    const auto behavior = state.behaviors.find(unitId);
    return behavior != state.behaviors.end()
        && behavior->second.kind == RuntimeBehaviorKind::Move
        && behavior->second.state != RuntimeBehaviorState::Complete
        && behavior->second.moveTarget
            == RuntimeCellPoint{expected.x, expected.y}
        && behavior->second.pendingAllowedOccupiedObjectId
            == (queued ? -1 : hostId);
}

RuntimeRepairDepotContext makeRuntimeRepairDepotContext(
    RuntimeProductionHostState& state,
    const RuntimeRepairDepotSystemState* authority) {
    RuntimeRepairDepotSystemState* liveAuthority =
        authority == nullptr ? &state.repairDepots : nullptr;
    const RuntimeRepairDepotSystemState* resolvedAuthority =
        authority == nullptr ? liveAuthority : authority;
    using RuntimeRepairDepotRallyKey = std::tuple<int, int, int>;
    auto rallyCellsByKey = std::make_shared<std::map<
        RuntimeRepairDepotRallyKey,
        RuntimeRepairDepotCell
    >>();
    auto reservedRallyCells = std::make_shared<
        std::set<std::tuple<int, int, bool>>
    >();
    RuntimeRepairDepotContext context;
    context.rules = &state.rules;
    context.objects = &state.world.getObjects();
    context.areAllied = [&state](
        const std::string& left,
        const std::string& right
    ) {
        return runtimeOwnersAreAllied(state, left, right);
    };
    context.creditBalance = [&state](const std::string& owner) {
        return runtimeRepairDepotCreditBalance(state, owner);
    };
    context.animationDurationTicks = [&state](int hostId, RuntimeRepairDepotAnimationPhase phase) {
        const auto* host = findObjectById(state, hostId);
        const auto* art = host ? runtimeArtTypeForObject(state, *host) : nullptr;
        const std::string key = phase == RuntimeRepairDepotAnimationPhase::Retracting
            ? "SpecialAnimThree" : "SpecialAnim";
        if (art) for (const auto& layer : art->specialAnimations) {
            if (layer.key != key) continue;
            const bool damaged = host->health > 0
                && static_cast<long long>(host->health) * 2 < runtimeMaxHealthForObject(state, *host);
            const auto* animationArt = state.art.findArtType(
                damaged && !layer.damagedImage.empty() ? layer.damagedImage : layer.image);
            if (animationArt) {
                const auto* building = state.rules.findBuildingType(host->type);
                return building && (building->naval || building->waterBound)
                    ? runtimeNavalRepairAnimationDuration(animationArt, state.config.fixedTickHz)
                    : runtimeRepairDepotAnimationDuration(animationArt, state.config.fixedTickHz);
            }
        }
        return std::max(1, state.config.fixedTickHz / 2);
    };
    context.resolveHostDomain = [&state](
        int hostId
    ) -> std::optional<RuntimeRepairDepotDomain> {
        const WorldObjectInstance* host = findObjectById(state, hostId);
        const BuildingType* building = host == nullptr
            ? nullptr
            : findRuntimeBuildingTypeById(state.rules, host->type);
        if (host == nullptr
            || !host->alive
            || host->health <= 0
            || host->kind != WorldObjectKind::Structure
            || building == nullptr
            || !building->unitRepair
            || building->numberOfDocks <= 0) {
            return std::nullopt;
        }
        return building->naval || building->waterBound
            ? RuntimeRepairDepotDomain::Naval
            : RuntimeRepairDepotDomain::Ground;
    };
    context.resolveUnitDomain = [&state](
        int unitId
    ) -> std::optional<RuntimeRepairDepotDomain> {
        const WorldObjectInstance* unit = findObjectById(state, unitId);
        const UnitType* rules = unit == nullptr
            ? nullptr
            : state.rules.findUnitType("UnitType", unit->type);
        if (unit == nullptr
            || !unit->alive
            || unit->health <= 0
            || unit->kind != WorldObjectKind::Unit
            || rules == nullptr) {
            return std::nullopt;
        }
        // VehicleTypes listed as PadAircraft share the physical slot but are
        // repaired/reloaded by AircraftService, never by the ground depot.
        if (findRuntimePadAircraftUnitType(state.rules, *unit) != nullptr) {
            return std::nullopt;
        }
        return rules->naval || rules->waterBound
            ? RuntimeRepairDepotDomain::Naval
            : RuntimeRepairDepotDomain::Ground;
    };
    context.refundValue = [&state](int unitId) -> std::optional<int> {
        const WorldObjectInstance* unit = findObjectById(state, unitId);
        const UnitType* rules = unit == nullptr
            ? nullptr
            : state.rules.findUnitType("UnitType", unit->type);
        if (unit == nullptr
            || !unit->alive
            || unit->health <= 0
            || unit->kind != WorldObjectKind::Unit
            || rules == nullptr) {
            return std::nullopt;
        }
        if (rules->soylent > 0) {
            return rules->soylent;
        }
        const long double raw =
            static_cast<long double>(std::max(0, rules->cost))
            * static_cast<long double>(state.rules.getRefundPercent());
        if (!std::isfinite(static_cast<double>(raw)) || raw <= 0.0L) {
            return 0;
        }
        if (raw >= static_cast<long double>(
                std::numeric_limits<int>::max())) {
            return std::numeric_limits<int>::max();
        }
        return static_cast<int>(std::floor(raw));
    };
    context.resolveDockCell = [&state](
        int hostId,
        int dockIndex,
        int unitId
    ) -> std::optional<RuntimeRepairDepotCell> {
        return runtimeRepairDepotDockCell(
            state,
            hostId,
            dockIndex,
            unitId
        );
    };
    context.resolveQueueCell = [&state](
        int hostId,
        int queueIndex,
        int unitId
    ) {
        return runtimeRepairDepotQueueCell(
            state,
            hostId,
            queueIndex,
            unitId
        );
    };
    context.resolveRallyCell = [
        &state,
        resolvedAuthority,
        rallyCellsByKey,
        reservedRallyCells
    ](
        int hostId,
        int dockIndex,
        int unitId
    ) {
        const RuntimeRepairDepotRallyKey key{
            hostId,
            dockIndex,
            unitId
        };
        const auto cached = rallyCellsByKey->find(key);
        if (cached != rallyCellsByKey->end()) {
            return std::optional<RuntimeRepairDepotCell>(cached->second);
        }
        const std::optional<RuntimeRepairDepotCell> resolved =
            runtimeRepairDepotRallyCell(
            state,
            hostId,
            dockIndex,
            unitId,
            *resolvedAuthority,
            reservedRallyCells.get()
        );
        if (resolved.has_value()) {
            rallyCellsByKey->emplace(key, *resolved);
            reservedRallyCells->insert({
                resolved->x,
                resolved->y,
                resolved->onBridge
            });
        }
        return resolved;
    };
    context.canPathTo = [&state, resolvedAuthority](
        int unitId,
        const RuntimeRepairDepotCell& target
    ) {
        const WorldObjectInstance* unit = findObjectById(state, unitId);
        if (unit == nullptr || !unit->alive) {
            return false;
        }
        const RuntimeCellPoint current = runtimeCellPointForObject(*unit);
        const RuntimeCellPoint goal{target.x, target.y};
        if (current == goal && unit->onBridge == target.onBridge) {
            return true;
        }
        const RuntimeRepairDepotRelation* activeRelation =
            findRuntimeRepairDepotRelationForUnit(
                *resolvedAuthority,
                unitId
            );
        bool queuedPromotionTarget = false;
        int queuedHostId = -1;
        if (activeRelation != nullptr) {
            const bool queued = activeRelation->phase
                == RuntimeRepairDepotRelationPhase::Queued;
            const RuntimeRepairDepotCell expected = queued
                ? activeRelation->queueCell
                : activeRelation->dockCell;
            const int slot = queued
                ? activeRelation->queueIndex
                : activeRelation->dockIndex;
            if (expected == target
                && runtimeRepairDepotReservationIsActive(
                    state,
                    *resolvedAuthority,
                    activeRelation->hostId,
                    slot,
                    unitId
                )) {
                return true;
            }
            queuedPromotionTarget = queued;
            queuedHostId = activeRelation->hostId;
        }
        std::optional<int> allowedHost =
            runtimeRepairDepotAllowedHostForCell(
                state,
                *resolvedAuthority,
                unitId,
                target
            );
        std::unordered_set<int> transientMobileBlockers;
        if (queuedPromotionTarget) {
            allowedHost = queuedHostId;
            for (const WorldObjectInstance& candidate :
                    state.world.getObjects()) {
                if (candidate.alive
                    && candidate.instanceId != unitId
                    && candidate.kind != WorldObjectKind::Structure) {
                    transientMobileBlockers.insert(candidate.instanceId);
                }
            }
        }
        const std::unordered_set<int>* ignoredObjectIds =
            transientMobileBlockers.empty()
                ? nullptr
                : &transientMobileBlockers;
        return !findRuntimePath(
            static_cast<const RuntimeProductionHostState&>(state),
            *unit,
            current,
            goal,
            allowedHost.value_or(-1),
            ignoredObjectIds
        ).empty();
    };
    context.isReservationActive = [&state, resolvedAuthority](
        int hostId,
        int phaseSlotIndex,
        int unitId
    ) {
        return runtimeRepairDepotReservationIsActive(
            state,
            *resolvedAuthority,
            hostId,
            phaseSlotIndex,
            unitId
        );
    };
    context.isTransported = [&state](int unitId) {
        for (const WorldObjectInstance& transport :
                state.world.getObjects()) {
            if (!transport.alive) {
                continue;
            }
            if (std::find(
                    transport.transportPassengerIds.begin(),
                    transport.transportPassengerIds.end(),
                    unitId
                ) != transport.transportPassengerIds.end()) {
                return true;
            }
        }
        return false;
    };
    context.canonicalOwnerId = [&state](const std::string& owner) {
        return runtimeRepairDepotCanonicalOwnerId(state, owner);
    };
    context.isDockOccupied = [&state](int hostId, int dockIndex) {
        return state.factoryDeliveries.claimsDock(hostId, dockIndex)
            || (dockIndex == 0
                && state.factoryDeliveries.claimsDock(hostId, -1))
            || std::any_of(
            state.world.getObjects().begin(),
            state.world.getObjects().end(),
            [hostId, dockIndex](const WorldObjectInstance& object) {
                return object.alive
                    && object.dockedToObjectId == hostId
                    && object.dockIndex == dockIndex;
            }
        );
    };
    return context;
}

bool runtimeSpyDisguiseBlocksAttack(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& observer,
    const WorldObjectInstance& target,
    bool autoAcquired) {
    if (!runtimeSpyDisguiseConcealsFromObject(state, observer, target)) {
        return false;
    }
    if (autoAcquired) {
        return true;
    }
    const RuntimeWeaponProfile profile = runtimeWeaponProfileForObject(
        state,
        observer,
        &target
    );
    if (profile.warhead != nullptr && profile.warhead->makesDisguise
        && runtimeSpyCanApplyMakesDisguise(
            state,
            observer.instanceId,
            target.instanceId,
            profile.warhead->id
        )) {
        return false;
    }
    if (state.rules.getSpecialMechanismRules().attackCursorOnDisguise) {
        return false;
    }
    const UnitType* targetRules = findRuntimeUnitType(state.rules, target);
    // The stock rule explicitly keeps a manual attack cursor on a Spy even
    // when generic disguised targets suppress it.
    return targetRules == nullptr || !targetRules->agent;
}

RuntimeRepairDepotContext makeRuntimeRepairDepotCandidateContext(
    RuntimeProductionHostState& state,
    const RuntimeRepairDepotSystemState& authority,
    const std::set<std::pair<int, int>>& blockedDocks) {
    RuntimeRepairDepotContext context = makeRuntimeRepairDepotContext(
        state,
        &authority
    );
    context.maximumQueuePromotions = 1U;
    const auto baseIsDockOccupied = std::move(context.isDockOccupied);
    context.isDockOccupied = [baseIsDockOccupied, &blockedDocks](
        int hostId,
        int dockIndex
    ) {
        return blockedDocks.contains({hostId, dockIndex})
            || (baseIsDockOccupied
                && baseIsDockOccupied(hostId, dockIndex));
    };
    return context;
}

bool runtimeGrindingEntryContinuesToHost(
    const RuntimeGameSessionState& state, int hostId, int sourceId) {
    const auto* source = findObjectById(state, sourceId);
    if (!source || runtimeObjectConflictsWithGrinding(state, sourceId)) return false;
    for (int id : source->transportPassengerIds) {
        if (runtimeObjectConflictsWithGrinding(state, id, sourceId)) return false;
    }
    return state.grinding.hasActiveEntry(makeRuntimeGrindingContext(state), hostId, sourceId);
}

void emitRuntimeGrindingCompletionSounds(
    RuntimeProductionHostState& state, const std::vector<RuntimeGrindingAction>& actions) noexcept {
    // Only committed removals reach here. A transport and its cargo are one
    // recycling event, and an audio failure cannot roll back its refund.
    try {
        const std::string sound = runtimeRulesSoundSection(state, "EnterGrinderSound", "GrinderGrinding");
        for (const auto& action : actions) {
            if (action.kind != RuntimeGrindingActionKind::RemoveUnit
                || action.removalRole != RuntimeGrindingRemovalRole::Root) continue;
            if (const auto* host = findObjectById(state, action.hostId)) {
                emitRuntimeSound(state, "sound:" + sound, runtimeSpatialAudioPositionForObject(*host));
            }
        }
    } catch (...) {
        // Sound is presentation; the consumed objects and credits are final.
    }
}

bool initializeRuntimeGrindingAnimation(RuntimeProductionHostState& state, int hostId,
    RuntimeImpactEffect& cycle) {
    const auto* host=findObjectById(state,hostId);
    if (!host) return false;
    const auto* building=findRuntimeBuildingType(state.rules,*host);
    const auto* art=runtimeArtTypeForObject(state,*host);
    if (!building || !building->grinding || !art) return false;
    const auto layer=std::find_if(art->specialAnimations.begin(),art->specialAnimations.end(),
        [](const auto& animation){return animation.key=="SpecialAnim";});
    if (layer==art->specialAnimations.end()) return false;
    const bool damaged=host->health*2<runtimeMaxHealthForObject(state,*host);
    cycle=RuntimeImpactEffect{};
    cycle.attackerObjectId=cycle.targetObjectId=hostId; cycle.owner=host->owner;
    cycle.cell={host->x,host->y}; cycle.level=host->level;
    cycle.animId=damaged && !layer->damagedImage.empty() ? layer->damagedImage : layer->image;
    cycle.renderIntent="grinderCycle"; cycle.lifecycleState="GrindingAnimation";
    initializeRuntimeImpactAnimationTimeline(state,cycle,true);
    if (cycle.animationFrameCount<=0) return false;
    // The SHP stores the healthy and damaged cycles together. End at the
    // authored loop boundary instead of continuing through the other half.
    cycle.animationEndFrame=cycle.animationLoopEndFrame;
    cycle.animationLoopCount=1;
    cycle.animationSequenceSteps=cycle.animationEndFrame-cycle.animationStartFrame;
    cycle.totalTicks=cycle.remainingTicks=cycle.animationSequenceSteps*cycle.animationFrameDelayTicks;
    return cycle.animationSequenceSteps>0 && cycle.totalTicks>0;
}

void startRuntimeGrindingAnimation(RuntimeProductionHostState& state, int hostId) {
    RuntimeImpactEffect cycle;
    if (!initializeRuntimeGrindingAnimation(state,hostId,cycle)) return;
    const bool busy=std::any_of(state.impactEffects.begin(),state.impactEffects.end(),
        [&](const auto& impact){return impact.targetObjectId==hostId && impact.remainingTicks>0
            && (impact.lifecycleState=="GrindingAnimation"
                || impact.lifecycleState=="GrindingAnimationQueued");});
    if (busy) {
        cycle.lifecycleState="GrindingAnimationQueued";
        cycle.renderIntent="grinderCycleQueued";
    }
    state.impactEffects.push_back(std::move(cycle));
}

bool promoteRuntimeGrindingAnimations(RuntimeProductionHostState& state) {
    std::unordered_set<int> activeHosts;
    for (const auto& cycle:state.impactEffects)
        if (cycle.lifecycleState=="GrindingAnimation" && cycle.remainingTicks>0)
            activeHosts.insert(cycle.targetObjectId);
    bool changed=false;
    for (auto& cycle:state.impactEffects) {
        if (cycle.lifecycleState!="GrindingAnimationQueued"
            || activeHosts.contains(cycle.targetObjectId)) continue;
        RuntimeImpactEffect started;
        if (initializeRuntimeGrindingAnimation(state,cycle.targetObjectId,started)) {
            cycle=std::move(started);
            activeHosts.insert(cycle.targetObjectId);
        } else {
            cycle.remainingTicks=0;
        }
        changed=true;
    }
    std::erase_if(state.impactEffects,[](const auto& cycle) {
        return cycle.lifecycleState=="GrindingAnimationQueued" && cycle.remainingTicks<=0;
    });
    return changed;
}

void writeRuntimeGrindingAnimations(const RuntimeGameSessionState& state, std::ostream& output) {
    std::vector<const RuntimeImpactEffect*> cycles;
    for (const auto& cycle:state.impactEffects)
        if (cycle.remainingTicks>0 && (cycle.lifecycleState=="GrindingAnimation"
            || cycle.lifecycleState=="GrindingAnimationQueued")) cycles.push_back(&cycle);
    if (cycles.empty()) return;
    output << "\n[GrindingAnimations]\nSchema=1\nCount=" << cycles.size() << '\n';
    for (std::size_t i=0;i<cycles.size();++i)
        output << i << '=' << runtimeMultiplayerSnapshotImpactRecord(*cycles[i])
            << '|' << iniHexEncode(cycles[i]->animId) << '\n';
}

bool restoreRuntimeGrindingAnimations(RuntimeGameSessionState& state, const IniFile& ini,
    std::string& error) {
    if (!ini.getSection("GrindingAnimations")) return true;
    const auto fail=[&](){error="invalid grinder animation queue";return false;};
    int schema=0,count=0;
    if (!parseRuntimeInt(ini.getValue("GrindingAnimations","Schema",""),schema) || schema!=1
        || !parseRuntimeInt(ini.getValue("GrindingAnimations","Count",""),count)
        || count<0 || count>100000) return fail();
    std::vector<RuntimeImpactEffect> restored;
    restored.reserve(static_cast<std::size_t>(count));
    std::unordered_set<int> activeHosts;
    for (int i=0;i<count;++i) {
        const std::string record=ini.getValue("GrindingAnimations",std::to_string(i),"");
        const auto separator=record.rfind('|');
        RuntimeImpactEffect cycle;
        if (separator==std::string::npos
            || !parseRuntimeMultiplayerSnapshotImpactRecord(record.substr(0,separator),cycle)
            || !iniHexDecode(record.substr(separator+1),cycle.animId,256U)) return fail();
        const bool queued=cycle.lifecycleState=="GrindingAnimationQueued";
        if ((!queued && cycle.lifecycleState!="GrindingAnimation")
            || cycle.targetObjectId<0 || cycle.attackerObjectId!=cycle.targetObjectId
            || cycle.remainingTicks<=0 || cycle.damage!=0 || cycle.death
            || cycle.animationSequenceSteps<=0 || cycle.animationStartFrame<0
            || cycle.animationEndFrame<=cycle.animationStartFrame
            || cycle.animationEndFrame>cycle.animationFrameCount
            || cycle.animationLoopEndFrame!=cycle.animationEndFrame
            || cycle.animationLoopCount!=1
            || cycle.animationSequenceSteps!=cycle.animationEndFrame-cycle.animationStartFrame
            || static_cast<long long>(cycle.animationSequenceSteps)*cycle.animationFrameDelayTicks
                !=cycle.totalTicks
            || (queued && (cycle.remainingTicks!=cycle.totalTicks
                || !activeHosts.contains(cycle.targetObjectId)))
            || (!queued && !activeHosts.insert(cycle.targetObjectId).second)) return fail();
        const auto* host=findObjectById(state,cycle.targetObjectId);
        const auto* building=host ? findRuntimeBuildingType(state.rules,*host) : nullptr;
        const auto* art=host ? runtimeArtTypeForObject(state,*host) : nullptr;
        bool matchingImage=false;
        if (art) for (const auto& layer:art->specialAnimations) {
            if (layer.key=="SpecialAnim" && (cycle.animId==layer.image
                || (!layer.damagedImage.empty() && cycle.animId==layer.damagedImage))) {
                matchingImage=true;
                break;
            }
        }
        if (!host || host->kind!=WorldObjectKind::Structure || !building || !building->grinding
            || !matchingImage || !state.art.findArtType(cycle.animId)) return fail();
        cycle.renderIntent=queued ? "grinderCycleQueued" : "grinderCycle";
        cycle.frameIndex=runtimeImpactAnimationFrameForStep(cycle,
            (cycle.totalTicks-cycle.remainingTicks)/cycle.animationFrameDelayTicks);
        restored.push_back(std::move(cycle));
    }
    state.impactEffects=std::move(restored);
    return true;
}

void queueRuntimeRepairDepotExactMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& unit,
    const RuntimeRepairDepotCell& target,
    int allowedOccupiedObjectId,
    bool remainsInRepairFlow,
    const std::string& reason) {
    const auto oldBehavior = state.behaviors.find(unit.instanceId);
    std::optional<RuntimeObjectBehavior> continuation;
    const bool navalSegment = unit.kind == WorldObjectKind::Unit && !unit.airborne
        && runtimeObjectMovementMode(state, unit) == PathMovementMode::Naval;
    if (oldBehavior != state.behaviors.end() && oldBehavior->second.moveCooldownTicks > 0
        && (runtimeObjectUsesVehicleArrival(state, unit) || navalSegment)) continuation = oldBehavior->second;
    if (oldBehavior != state.behaviors.end()) {
        clearRuntimeTemporalAttack(
            state,
            unit.instanceId,
            oldBehavior->second,
            "repair depot action interrupted attack"
        );
    }
    eraseRuntimeBehavior(state, unit.instanceId);
    RuntimeObjectBehavior behavior;
    behavior.kind = RuntimeBehaviorKind::Move;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.moveTarget = {target.x, target.y};
    behavior.pendingAllowedOccupiedObjectId = allowedOccupiedObjectId;
    if (continuation) inheritRuntimeVehicleMoveSegment(unit, behavior, *continuation);
    setRuntimeMissionIntent(
        state,
        unit,
        remainsInRepairFlow
            ? RuntimeMissionIntent::Repairing
            : RuntimeMissionIntent::Idle,
        reason
    );
    state.behaviors[unit.instanceId] = std::move(behavior);
    RuntimeObjectBehavior& queued = state.behaviors[unit.instanceId];
    submitRuntimeMovePathRequest(
        state,
        unit,
        queued,
        runtimeCellPointForObject(unit),
        queued.moveTarget,
        allowedOccupiedObjectId
    );
}

bool applyRuntimeRepairDepotActions(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeRepairDepotAction>& actions) {
    if (std::any_of(
            actions.begin(),
            actions.end(),
            [](const RuntimeRepairDepotAction& action) {
                return action.kind
                    == RuntimeRepairDepotActionKind::TransferDockedUnitOwner;
            }
        )) {
        // Captured repair-host ownership is materialized only by the
        // detached owner-transition transaction.  Replaying this action
        // against live state would split repair and world authority.
        return false;
    }
    std::set<std::string> padProductionOwnersToRevalidate;
    for (const RuntimeRepairDepotAction& action : actions) {
        if (action.kind != RuntimeRepairDepotActionKind::MoveToDock) {
            continue;
        }
        const WorldObjectInstance* host = findObjectById(state, action.hostId);
        if (host == nullptr) {
            return false;
        }
        const std::string hostOwner = runtimeRepairDepotCanonicalOwnerId(
            state,
            host->owner
        );
        if (hostOwner == runtimeRepairDepotCanonicalOwnerId(
                state,
                state.economy.owner
            )) {
            padProductionOwnersToRevalidate.insert(hostOwner);
        }
    }
    for (const std::string& owner : padProductionOwnersToRevalidate) {
        const RuntimeAircraftProductionMatchResult matched =
            runtimeAircraftProductionMatchForOwner(state, owner);
        if (!matched.inputValid || !matched.feasible) {
            // The caller still owns the pre-action repair snapshot and will
            // restore it. No world/action mutation has occurred yet.
            return false;
        }
    }
    struct ShadowUnit {
        bool exists = false;
        bool alive = false;
        WorldObjectKind kind = WorldObjectKind::Unit;
        int health = 0;
        int maxHealth = 1;
        int dockedToObjectId = -1;
        int dockIndex = -1;
        int x = -1;
        int y = -1;
        bool onBridge = false;
        std::string owner;
    };
    std::unordered_map<int, ShadowUnit> shadowUnits;
    std::unordered_map<std::string, int> shadowCredits;
    const auto shadowUnit = [&state, &shadowUnits](int unitId)
            -> ShadowUnit* {
        auto found = shadowUnits.find(unitId);
        if (found != shadowUnits.end()) {
            return &found->second;
        }
        const WorldObjectInstance* unit =
            findRuntimeObjectByIdIncludingInactive(state, unitId);
        ShadowUnit shadow;
        if (unit != nullptr) {
            shadow.exists = true;
            shadow.alive = unit->alive;
            shadow.kind = unit->kind;
            shadow.health = unit->health;
            shadow.maxHealth = std::max(
                1,
                runtimeMaxHealthForObject(state, *unit)
            );
            shadow.dockedToObjectId = unit->dockedToObjectId;
            shadow.dockIndex = unit->dockIndex;
            shadow.x = unit->x;
            shadow.y = unit->y;
            shadow.onBridge = unit->onBridge;
            shadow.owner = unit->owner;
        }
        return &shadowUnits.emplace(unitId, std::move(shadow)).first->second;
    };
    const auto availableCredits = [&state, &shadowCredits](
        const std::string& owner
    ) -> int& {
        const std::string key =
            runtimeRepairDepotCanonicalOwnerId(state, owner);
        auto found = shadowCredits.find(key);
        if (found == shadowCredits.end()) {
            found = shadowCredits.emplace(
                key,
                runtimeRepairDepotCreditBalance(state, key)
            ).first;
        }
        return found->second;
    };

    // Validate the full ordered batch against shadows.  Repair followed by
    // Undock in one tick is therefore checked without exposing half-applied
    // health, credits or docking state to the world.
    for (const RuntimeRepairDepotAction& action : actions) {
        ShadowUnit* unit = shadowUnit(action.unitId);
        if (unit == nullptr || !unit->exists) {
            if (action.kind == RuntimeRepairDepotActionKind::Release) {
                continue;
            }
            return false;
        }
        if (action.hasTargetCell
            && (action.targetCell.x < 0 || action.targetCell.y < 0)) {
            return false;
        }
        if (action.kind == RuntimeRepairDepotActionKind::MoveToQueue
            || action.kind == RuntimeRepairDepotActionKind::MoveToDock) {
            const bool queueMove =
                action.kind == RuntimeRepairDepotActionKind::MoveToQueue;
            const RuntimeRepairDepotRelation* relation =
                findRuntimeRepairDepotRelationForUnit(
                    state.repairDepots,
                    action.unitId
                );
            const RuntimeRepairDepotCell expected = relation == nullptr
                ? RuntimeRepairDepotCell{}
                : queueMove ? relation->queueCell : relation->dockCell;
            if (!unit->alive
                || unit->kind != WorldObjectKind::Unit
                || relation == nullptr
                || relation->hostId != action.hostId
                || relation->phase != (queueMove
                    ? RuntimeRepairDepotRelationPhase::Queued
                    : RuntimeRepairDepotRelationPhase::Reserved)
                || (queueMove ? relation->queueIndex : relation->dockIndex)
                    != (queueMove ? action.queueIndex : action.dockIndex)
                || !action.hasTargetCell
                || action.targetCell != expected) {
                return false;
            }
            continue;
        }
        if (action.kind == RuntimeRepairDepotActionKind::Attach) {
            const RuntimeRepairDepotRelation* relation =
                findRuntimeRepairDepotRelationForUnit(
                    state.repairDepots,
                    action.unitId
                );
            const WorldObjectInstance* host =
                findObjectById(state, action.hostId);
            if (!unit->alive
                || unit->kind != WorldObjectKind::Unit
                || host == nullptr
                || !host->alive
                || relation == nullptr
                || relation->phase != RuntimeRepairDepotRelationPhase::Docked
                || relation->hostId != action.hostId
                || relation->dockIndex != action.dockIndex
                || !action.hasTargetCell
                || relation->dockCell != action.targetCell
                || unit->x != action.targetCell.x
                || unit->y != action.targetCell.y
                || unit->onBridge != action.targetCell.onBridge
                || unit->dockedToObjectId >= 0
                || unit->dockIndex >= 0) {
                return false;
            }
            unit->dockedToObjectId = action.hostId;
            unit->dockIndex = action.dockIndex;
            continue;
        }
        if (action.kind == RuntimeRepairDepotActionKind::Repair) {
            const std::string owner =
                runtimeRepairDepotCanonicalOwnerId(state, unit->owner);
            if (!unit->alive
                || unit->kind != WorldObjectKind::Unit
                || unit->dockedToObjectId != action.hostId
                || unit->dockIndex != action.dockIndex
                || action.owner != owner
                || action.healthBefore != unit->health
                || action.healthAfter < action.healthBefore
                || action.healthAfter > unit->maxHealth
                || action.healedHitPoints
                    != action.healthAfter - action.healthBefore
                || action.creditCost < 0
                || availableCredits(owner) < action.creditCost) {
                return false;
            }
            availableCredits(owner) -= action.creditCost;
            unit->health = action.healthAfter;
            continue;
        }
        if (action.kind
                == RuntimeRepairDepotActionKind::DestroyDockedUnit
            || action.kind
                == RuntimeRepairDepotActionKind::SellDockedUnit) {
            const bool selling = action.kind
                == RuntimeRepairDepotActionKind::SellDockedUnit;
            const std::string owner =
                runtimeRepairDepotCanonicalOwnerId(state, unit->owner);
            if (!unit->alive
                || unit->kind != WorldObjectKind::Unit
                || unit->dockedToObjectId != action.hostId
                || unit->dockIndex != action.dockIndex
                || action.domain != RuntimeRepairDepotDomain::Ground
                || state.deathWeapons.hasTriggered(action.unitId)
                || state.pendingObjectExitIds.find(action.unitId)
                    != state.pendingObjectExitIds.end()
                || (selling
                    && (runtimeRepairDepotCanonicalOwnerId(
                            state,
                            action.owner
                        ) != owner
                        || action.refundValue < 0))) {
                return false;
            }
            if (selling) {
                availableCredits(owner) = runtimeCreditBalanceAfterDeposit(
                    availableCredits(owner),
                    action.refundValue
                );
            }
            unit->alive = false;
            unit->health = 0;
            unit->dockedToObjectId = -1;
            unit->dockIndex = -1;
            continue;
        }
        if (action.kind == RuntimeRepairDepotActionKind::UndockToRally) {
            const bool physicallyDocked =
                unit->dockedToObjectId == action.hostId
                && unit->dockIndex == action.dockIndex;
            const std::optional<RuntimeRepairDepotCell> dock =
                runtimeRepairDepotDockCell(
                    state,
                    action.hostId,
                    action.dockIndex
                );
            const bool arrivedButNotAttached =
                unit->dockedToObjectId < 0
                && unit->dockIndex < 0
                && dock.has_value()
                && unit->x == dock->x
                && unit->y == dock->y
                && unit->onBridge == dock->onBridge;
            if (!unit->alive
                || (!physicallyDocked && !arrivedButNotAttached)) {
                return false;
            }
            unit->dockedToObjectId = -1;
            unit->dockIndex = -1;
            continue;
        }
        if (action.kind == RuntimeRepairDepotActionKind::Release) {
            if (unit->dockedToObjectId == action.hostId
                && unit->dockIndex == action.dockIndex) {
                unit->dockedToObjectId = -1;
                unit->dockIndex = -1;
            }
            continue;
        }
        return false;
    }

    // Validate parasite removal before committing any repair batch effects.
    // Passive self-healing differs from this paid external healing service.
    for (const RuntimeRepairDepotAction& action : actions) {
        if (action.kind == RuntimeRepairDepotActionKind::Repair
            && action.healedHitPoints > 0
            && !preflightRuntimeParasiteHostEvent(state, action.unitId,
                ParasiteHostEventKind::Healed, std::max(1, state.config.fixedTickHz))) {
            return false;
        }
    }
    bool occupancyChanged = false;
    bool projectionChanged = false;
    for (const RuntimeRepairDepotAction& action : actions) {
        if (action.kind == RuntimeRepairDepotActionKind::Release) {
            std::ostringstream diagnostic;
            diagnostic
                << "repair-release #" << action.unitId
                << " reason=" << action.reason
                << " host=" << action.hostId
                << " tick=" << state.tick;
            state.capabilityCache.recentMissionDiagnostics.push_back(
                diagnostic.str()
            );
            constexpr std::size_t maxMissionDiagnostics = 12;
            if (state.capabilityCache.recentMissionDiagnostics.size()
                    > maxMissionDiagnostics) {
                state.capabilityCache.recentMissionDiagnostics.erase(
                    state.capabilityCache.recentMissionDiagnostics.begin(),
                    state.capabilityCache.recentMissionDiagnostics.begin()
                        + static_cast<std::ptrdiff_t>(
                            state.capabilityCache.recentMissionDiagnostics.size()
                            - maxMissionDiagnostics
                        )
                );
            }
        }
        WorldObjectInstance* unit =
            findMutableRuntimeObjectByIdIncludingInactive(
                state,
                action.unitId
            );
        if (unit == nullptr) {
            continue;
        }
        if (action.kind == RuntimeRepairDepotActionKind::MoveToQueue
            || action.kind == RuntimeRepairDepotActionKind::MoveToDock) {
            queueRuntimeRepairDepotExactMove(
                state,
                *unit,
                action.targetCell,
                action.kind == RuntimeRepairDepotActionKind::MoveToDock
                    ? action.hostId
                    : -1,
                true,
                action.kind == RuntimeRepairDepotActionKind::MoveToDock
                    ? "repairDepotDockMove"
                    : "repairDepotQueueMove"
            );
            continue;
        }
        if (action.kind == RuntimeRepairDepotActionKind::Attach) {
            unit->x = action.targetCell.x;
            unit->y = action.targetCell.y;
            unit->cell = runtimeCellKey(unit->x, unit->y);
            unit->onBridge = action.targetCell.onBridge;
            unit->dockedToObjectId = action.hostId;
            unit->dockIndex = action.dockIndex;
            clearRuntimeRenderPosition(*unit);
            if (const auto* host = findObjectById(state, action.hostId))
                applyRuntimeRepairDepotDockPose(state, *unit, *host, action.dockIndex);
            setRuntimeMissionIntent(
                state,
                *unit,
                RuntimeMissionIntent::Repairing,
                "repairDepotAttached"
            );
            occupancyChanged = true;
            projectionChanged = true;
            continue;
        }
        if (action.kind == RuntimeRepairDepotActionKind::Repair) {
            if (action.healedHitPoints > 0) {
                if (!handleRuntimeParasiteHostEvent(state, action.unitId,
                        ParasiteHostEventKind::Healed, std::max(1, state.config.fixedTickHz))) {
                    return false;
                }
                unit = findMutableRuntimeObjectByIdIncludingInactive(state, action.unitId);
                if (unit == nullptr) return false;
            }
            debitRuntimeRepairDepotCredits(
                state,
                action.owner,
                action.creditCost
            );
            unit->health = action.healthAfter;
            state.economy.status =
                "repairing " + unit->type
                + " hp " + std::to_string(action.healthAfter)
                + "/" + std::to_string(
                    runtimeMaxHealthForObject(state, *unit)
                )
                + " cost " + std::to_string(action.creditCost);
            if (action.firstRepairPulse) {
                emitRuntimeSound(state, "Repairing");
            }
            projectionChanged = true;
            state.repaintRequested = true;
            continue;
        }
        if (action.kind
                == RuntimeRepairDepotActionKind::DestroyDockedUnit
            || action.kind
                == RuntimeRepairDepotActionKind::SellDockedUnit) {
            RuntimeObjectExitRequest objectExit;
            objectExit.objectId = action.unitId;
            objectExit.kind = action.kind
                    == RuntimeRepairDepotActionKind::SellDockedUnit
                ? RuntimeObjectExitKind::Sold
                : RuntimeObjectExitKind::Destroyed;
            objectExit.tick = state.tick;
            objectExit.mission = objectExit.kind
                    == RuntimeObjectExitKind::Sold
                ? "Sold"
                : "Destroyed";
            objectExit.reason = action.reason;
            objectExit.sourceObjectId = action.hostId;
            const WorldObjectInstance* host =
                findRuntimeObjectByIdIncludingInactive(
                    state,
                    action.hostId
                );
            objectExit.sourceOwner = host == nullptr
                ? std::string()
                : host->owner;
            objectExit.suppressDeathWeapon = objectExit.kind
                == RuntimeObjectExitKind::Sold;
            if (queueRuntimeObjectExit(state, std::move(objectExit))
                    != RuntimeObjectExitQueueResult::Queued) {
                return false;
            }
            if (action.kind
                    == RuntimeRepairDepotActionKind::SellDockedUnit) {
                depositRuntimeRepairDepotCredits(
                    state,
                    action.owner,
                    action.refundValue
                );
            }
            continue;
        }
        const bool wasDocked = unit->dockedToObjectId == action.hostId
            && unit->dockIndex == action.dockIndex;
        if (wasDocked) {
            unit->dockedToObjectId = -1;
            unit->dockIndex = -1;
            // Both ground pads and naval docks have fractional authored poses.
            // The next movement segment starts from this pose; clearing it here
            // would snap a departing ship back to the integer navigation cell.
            occupancyChanged = true;
            projectionChanged = true;
        }
        const bool repairBehavior =
            runtimeBehaviorMatchesRepairAction(state, action);
        if (unit->alive && action.hasTargetCell) {
            queueRuntimeRepairDepotExactMove(
                state,
                *unit,
                action.targetCell,
                -1,
                false,
                action.kind == RuntimeRepairDepotActionKind::UndockToRally
                    ? "repairDepotCompleteRally"
                    : action.reason.empty()
                        ? "repairDepotReleaseRally"
                        : "repairDepotReleaseRally:" + action.reason
            );
        } else if (repairBehavior
            || wasDocked
            || action.kind == RuntimeRepairDepotActionKind::UndockToRally) {
            eraseRuntimeBehavior(state, action.unitId);
            if (unit->alive) {
                setRuntimeMissionIntent(
                    state,
                    *unit,
                    RuntimeMissionIntent::Idle,
                    action.reason.empty()
                        ? "repairDepotReleased"
                        : "repairDepotReleased:" + action.reason
                );
            }
        }
        if (action.kind == RuntimeRepairDepotActionKind::UndockToRally) {
            emitRuntimeSound(state, runtimeSoundIntentStructureRepaired);
        }
    }
    if (occupancyChanged) {
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
    }
    if (projectionChanged) {
        invalidateRuntimeObjectProjectionCache(state);
    }
    refreshRuntimeActionSummary(state);
    return true;
}

bool releaseRuntimeRepairDepotForCommand(
    RuntimeProductionHostState& state,
    int unitId,
    const std::string& reason) {
    if (!state.repairDepotsInitialized
        || findRuntimeRepairDepotRelationForUnit(
            state.repairDepots,
            unitId
        ) == nullptr) {
        return true;
    }
    const RuntimeRepairDepotSystemState before = state.repairDepots;
    RuntimeRepairDepotContext context = makeRuntimeRepairDepotContext(state);
    context.allowQueuePromotion = false;
    const std::vector<RuntimeRepairDepotAction> actions =
        state.repairDepots.releaseUnit(context, unitId, reason);
    if (!applyRuntimeRepairDepotActions(state, actions)) {
        state.repairDepots = before;
        return false;
    }
    return true;
}

bool releaseRuntimeRepairDepotRelationsForObject(
    RuntimeProductionHostState& state,
    int objectId,
    const std::string& reason) {
    if (!state.repairDepotsInitialized) {
        return true;
    }
    std::vector<int> unitIds;
    for (const RuntimeRepairDepotRelation& relation :
            state.repairDepots.relations()) {
        if (relation.hostId == objectId || relation.unitId == objectId) {
            unitIds.push_back(relation.unitId);
        }
    }
    std::sort(unitIds.begin(), unitIds.end());
    unitIds.erase(std::unique(unitIds.begin(), unitIds.end()), unitIds.end());
    for (int unitId : unitIds) {
        if (!releaseRuntimeRepairDepotForCommand(state, unitId, reason)) {
            return false;
        }
    }
    return true;
}

bool transitionRuntimeRepairDepotHost(
    RuntimeProductionHostState& state,
    int hostId,
    RuntimeRepairDepotHostTransitionKind kind,
    bool temporalRemoval,
    const std::string& expectedOwner,
    const std::string& newOwner) {
    if (kind == RuntimeRepairDepotHostTransitionKind::Captured) {
        state.simulationStatus =
            "repair depot capture requires owner transition batch";
        return false;
    }
    if (!state.repairDepotsInitialized) {
        return true;
    }
    const bool hasHostRelation = std::any_of(
        state.repairDepots.relations().begin(),
        state.repairDepots.relations().end(),
        [hostId](const RuntimeRepairDepotRelation& relation) {
            return relation.hostId == hostId;
        }
    );
    if (!hasHostRelation) {
        if (kind == RuntimeRepairDepotHostTransitionKind::Captured) {
            (void)state.factoryDeliveries.releaseByHost(hostId);
        }
        return true;
    }

    const RuntimeRepairDepotSystemState before = state.repairDepots;
    RuntimeRepairDepotContext context = makeRuntimeRepairDepotContext(state);
    if (kind == RuntimeRepairDepotHostTransitionKind::Destroyed) {
        // Super-weapon effects historically set alive=false before they
        // enqueue the authoritative exit.  Domain is authored Rules data, so
        // it remains safe to resolve while finishing that already-dead host.
        context.resolveHostDomain = [&state](
            int candidateId
        ) -> std::optional<RuntimeRepairDepotDomain> {
            const WorldObjectInstance* host =
                findRuntimeObjectByIdIncludingInactive(state, candidateId);
            const BuildingType* building = host == nullptr
                ? nullptr
                : findRuntimeBuildingTypeById(state.rules, host->type);
            if (host == nullptr
                || host->kind != WorldObjectKind::Structure
                || building == nullptr
                || !building->unitRepair
                || building->numberOfDocks <= 0) {
                return std::nullopt;
            }
            return building->naval || building->waterBound
                ? RuntimeRepairDepotDomain::Naval
                : RuntimeRepairDepotDomain::Ground;
        };
    }

    RuntimeRepairDepotHostTransitionRequest request;
    request.hostId = hostId;
    request.kind = kind;
    request.temporalRemoval = temporalRemoval;
    request.expectedOwner = expectedOwner;
    request.newOwner = newOwner;
    const RuntimeRepairDepotHostTransitionResult result =
        state.repairDepots.transitionHost(context, request);
    if (!result.accepted
        || !applyRuntimeRepairDepotActions(state, result.actions)) {
        state.repairDepots = before;
        state.simulationStatus = result.reason.empty()
            ? "repair depot lifecycle transition rejected atomically"
            : "repair depot lifecycle rejected: " + result.reason;
        return false;
    }
    if (kind == RuntimeRepairDepotHostTransitionKind::Captured) {
        (void)state.factoryDeliveries.releaseByHost(hostId);
    }
    return true;
}

RuntimeDeathWeaponContext makeRuntimeDeathWeaponContext(
    const RuntimeGameSessionState& state) {
    RuntimeDeathWeaponContext context;
    context.rules = &state.rules;
    context.objects = &state.world.getObjects();
    context.maximumCascadeDepth = 16;
    return context;
}

bool spawnRuntimeDeathWeaponProjectile(
    RuntimeProductionHostState& state,
    const RuntimeDeathWeaponFireRequest& request) {
    const WeaponType* weapon =
        findRuntimeWeaponType(state.rules, request.weaponId);
    if (weapon == nullptr) {
        return false;
    }

    WorldObjectInstance source;
    source.instanceId = request.sourceObjectId;
    source.kind = request.sourceKind;
    source.authoredAircraft = request.sourceAuthoredAircraft;
    source.owner = request.sourceOwner;
    source.type = request.sourceType;
    source.mapId = request.sourceMapId;
    source.tag = request.sourceTag;
    source.x = request.x;
    source.y = request.y;
    source.cell = request.cell;
    source.level = request.level;
    source.onBridge = request.onBridge;
    source.health = 1;
    source.alive = true;
    source.mission = "DeathWeapon";

    RuntimeWeaponProfile profile;
    finalizeRuntimeWeaponProfile(
        profile,
        state,
        RuntimeWeaponSlot::Indexed,
        *weapon,
        "deathWeapon"
    );
    spawnRuntimeProjectile(
        state,
        source,
        source,
        *weapon,
        profile,
        request.modifiedDamage,
        -1,
        0,
        false,
        -1,
        1.0,
        request.cascadeId,
        request.requestId,
        request.cascadeDepth,
        false
    );
    return true;
}

bool runtimeParasiteObjectIsContained(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (object.garrisonHostObjectId >= 0
        || !object.garrisonOccupantIds.empty()
        || !object.transportPassengerIds.empty()) {
        return true;
    }
    for (const WorldObjectInstance& candidate : state.world.getObjects()) {
        if (std::find(
                candidate.transportPassengerIds.begin(),
                candidate.transportPassengerIds.end(),
                object.instanceId
            ) != candidate.transportPassengerIds.end()) {
            return true;
        }
    }
    return false;
}

bool runtimeParasiteObjectHasExclusiveAuthority(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    bool asHost) {
    const auto stun = state.parasiteStunUntilTickByObject.find(
        object.instanceId
    );
    if ((stun != state.parasiteStunUntilTickByObject.end()
            && stun->second > state.tick)
        || runtimeParasiteObjectIsContained(state, object)
        || object.dockedToObjectId >= 0 || object.dockIndex >= 0
        || state.pendingEngineerCaptures.find(object.instanceId)
            != state.pendingEngineerCaptures.end()
        || state.pendingOccupyTargets.find(object.instanceId)
            != state.pendingOccupyTargets.end()
        || state.pendingTransportTargets.find(object.instanceId)
            != state.pendingTransportTargets.end()
        || state.pendingTimedBombIntents.find(object.instanceId)
            != state.pendingTimedBombIntents.end()) {
        return true;
    }
    if (state.repairDepotsInitialized) {
        for (const RuntimeRepairDepotRelation& relation :
                state.repairDepots.relations()) {
            if (relation.unitId == object.instanceId
                || relation.hostId == object.instanceId) {
                return true;
            }
        }
    }
    for (const RuntimeFactoryDeliveryLease& lease :
            state.factoryDeliveries.leases()) {
        if (lease.producedObjectId == object.instanceId
            || lease.hostId == object.instanceId) {
            return true;
        }
    }
    if (state.specialMechanismsInitialized
        && (state.specialMechanisms.isCarrierChild(object.instanceId)
            // The host stays in the world, so its aircraft can remain attached.
            // A parasite source would enter limbo and cannot own such children.
            || (!asHost && state.specialMechanisms.carrierParents().find(
                object.instanceId
            ) != state.specialMechanisms.carrierParents().end()))) {
        return true;
    }
    return false;
}

bool runtimeParasiteWarheadIsExclusive(
    const WeaponType& weapon,
    const WarheadType& warhead) {
    return weapon.limboLaunch && warhead.parasite
        && std::max(1, weapon.burst) == 1
        && !weapon.spawner
        && !weapon.sonic
        && !weapon.radBeam
        && !weapon.electricBolt
        && !weapon.magBeam
        && !warhead.psychedelic
        && !warhead.psychicDamage
        && !warhead.ivanBomb
        && !warhead.bombDisarm
        && !warhead.airstrike
        && !warhead.isLocomotor
        && !warhead.mindControl
        && !warhead.temporal
        && !warhead.radiation
        && !warhead.electricAssault
        && trimAsciiLocal(warhead.particle).empty()
        && trimAsciiLocal(warhead.particleSystem).empty();
}

bool runtimeProjectileIsParasiteLeap(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile) {
    const WeaponType* weapon =
        findRuntimeWeaponType(state.rules, projectile.weaponId);
    const WarheadType* warhead =
        findRuntimeWarheadType(state.rules, projectile.warheadId);
    return weapon != nullptr && warhead != nullptr
        && weapon->projectile == projectile.projectileId
        && runtimeParasiteWarheadIsExclusive(*weapon, *warhead);
}

void syncRuntimeParasiteLeapTargetPose(
    const RuntimeGameSessionState& state,
    RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& target) {
    (void)state;
    const auto subcell = worldObjectSubCellOffset(target);
    projectile.toCell = runtimeCellPointForObject(target);
    projectile.hasTargetPosition = true;
    projectile.targetX = runtimeObjectRenderX(target) + subcell.x;
    projectile.targetY = runtimeObjectRenderY(target) + subcell.y;
    projectile.targetZ = runtimeDrawLevelForObject(target);
    projectile.toLevel = target.level;
}

std::optional<RuntimeCellPoint> runtimeParasitePlacementCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const SpecialCell& requested,
    const std::unordered_set<int>* reservedCells,
    int terminatingHostId) {
    if (requested.x < 0 || requested.y < 0) {
        return std::nullopt;
    }
    // Exclude just the host being replaced. Rebuild the ordinary occupancy
    // lookup so any other body or terrain object in that cell still blocks it.
    const std::unordered_set<int> replacedHost{terminatingHostId};
    return findRuntimePassableCellNear(
        state,
        source,
        {requested.x, requested.y},
        6,
        -1,
        reservedCells,
        terminatingHostId >= 0 ? &replacedHost : nullptr
    );
}

std::vector<SpecialMechanismAction> prepareRuntimeParasiteActions(
    const RuntimeGameSessionState& state,
    const std::vector<SpecialMechanismAction>& actions) {
    std::vector<SpecialMechanismAction> prepared = actions;
    std::unordered_set<int> reservedPlacementCells;
    for (SpecialMechanismAction& action : prepared) {
        if (action.kind
                != SpecialMechanismActionKind::ParasiteReturnFromLimbo
            && action.kind != SpecialMechanismActionKind::ParasiteEject) {
            continue;
        }
        const WorldObjectInstance* source =
            findRuntimeObjectByIdIncludingInactive(state, action.objectId);
        const std::optional<RuntimeCellPoint> placement =
            source == nullptr
                ? std::nullopt
                : runtimeParasitePlacementCell(
                    state,
                    *source,
                    action.cell,
                    &reservedPlacementCells,
                    action.replacesTerminatingHost
                        ? action.targetObjectId : -1
                );
        if (!placement.has_value()) {
            action.kind = SpecialMechanismActionKind::ParasiteKillParasite;
            action.suppressDeathWeapon = true;
            action.replacesTerminatingHost = false;
            action.stunTicks = 0;
            action.reason += "; no legal host placement";
            continue;
        }
        action.cell = {placement->x, placement->y};
        reservedPlacementCells.insert(
            runtimeCellKey(placement->x, placement->y)
        );
    }
    return prepared;
}

bool validateRuntimeParasiteActionBatch(
    const RuntimeGameSessionState& state,
    const std::vector<SpecialMechanismAction>& actions) {
    std::unordered_set<int> returningSources;
    std::unordered_set<int> terminalObjects;
    for (const SpecialMechanismAction& action : actions) {
        if (action.kind == SpecialMechanismActionKind::TimedBombDetonate) {
            continue;
        }
        if (action.tick < 0 || action.projectileId == 0
            || action.objectId < 0 || action.targetObjectId < 0) {
            return false;
        }
        const WorldObjectInstance* source =
            findRuntimeObjectByIdIncludingInactive(state, action.objectId);
        const WorldObjectInstance* target =
            findRuntimeObjectByIdIncludingInactive(
                state,
                action.targetObjectId
            );
        if (action.replacesTerminatingHost
            && (source == nullptr || target == nullptr
                || source->instanceId == target->instanceId
                || action.stunTicks != 0
                || !((action.kind
                        == SpecialMechanismActionKind::ParasiteReturnFromLimbo
                        && target->kind == WorldObjectKind::Infantry)
                    || (action.kind == SpecialMechanismActionKind::ParasiteEject
                        && target->kind == WorldObjectKind::Unit)))) {
            return false;
        }
        if (action.sourceWasAttachedToHost
            && action.kind != SpecialMechanismActionKind::ParasiteEject
            && action.kind != SpecialMechanismActionKind::ParasiteKillParasite) {
            return false;
        }
        if (source != nullptr && !action.owner.empty()
            && runtimeRepairDepotCanonicalOwnerId(state, source->owner)
                != runtimeRepairDepotCanonicalOwnerId(
                    state,
                    action.owner
                )) {
            return false;
        }
        switch (action.kind) {
        case SpecialMechanismActionKind::ParasiteEnterLimbo:
            if (source == nullptr || !source->alive || source->health <= 0
                || state.pendingObjectExitIds.find(source->instanceId)
                    != state.pendingObjectExitIds.end()
                || runtimeParasiteObjectHasExclusiveAuthority(
                    state,
                    *source
                )) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteAttachHost:
            if (source == nullptr || source->alive || source->health <= 0
                || source->mission != "ParasiteLimbo"
                || target == nullptr || !target->alive
                || target->health <= 0
                || runtimeParasiteObjectHasExclusiveAuthority(
                    state,
                    *target,
                    true
                )) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteReturnFromLimbo:
        case SpecialMechanismActionKind::ParasiteEject:
            if (source == nullptr || source->alive || source->health <= 0
                || source->mission != "ParasiteLimbo"
                || !returningSources.insert(source->instanceId).second
                || !runtimeParasitePlacementCell(
                    state,
                    *source,
                    action.cell,
                    nullptr,
                    action.replacesTerminatingHost
                        ? action.targetObjectId : -1
                ).has_value()) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteDamage:
            if (target == nullptr || !target->alive || target->health <= 0
                || action.damage < 0) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteKillTarget:
            if (target == nullptr || !target->alive || target->health <= 0
                || !terminalObjects.insert(target->instanceId).second
                || state.pendingObjectExitIds.find(target->instanceId)
                    != state.pendingObjectExitIds.end()) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteKillParasite:
            if (source != nullptr && source->health > 0
                && (!terminalObjects.insert(source->instanceId).second
                    || state.pendingObjectExitIds.find(source->instanceId)
                        != state.pendingObjectExitIds.end())) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteReleaseTarget:
            break;
        case SpecialMechanismActionKind::TimedBombDetonate:
            break;
        }
    }
    return true;
}

bool runtimeObjectReselectsAfterParasiteLimbo(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const UnitType* rules = findRuntimeUnitType(state.rules, object);
    return rules != nullptr && rules->reselectIfLimboed;
}

bool runtimeObjectRejoinsTeamAfterParasiteLimbo(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const UnitType* rules = findRuntimeUnitType(state.rules, object);
    return rules != nullptr && rules->rejoinTeamIfLimboed;
}

RuntimeParasiteActionApplyResult applyRuntimeParasiteActionsAtomically(
    RuntimeProductionHostState& state,
    const std::vector<SpecialMechanismAction>& actions,
    const RuntimeDeathWeaponCause& inheritedDeathWeaponCause,
    int fallbackSourceObjectId,
    bool suppressUnitDeathSounds) {
    RuntimeParasiteActionApplyResult result;
    if (actions.empty()) {
        return result;
    }
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(state, actions);
    if (!validateRuntimeParasiteActionBatch(state, prepared)) {
        result.accepted = false;
        state.simulationStatus =
            "parasite action batch rejected atomically";
        return result;
    }

    RuntimeSpecialMechanismState parasiteDeathCandidate =
        state.specialMechanisms;
    std::vector<SpecialMechanismAction> parasiteDeathActions;
    bool hasParasiteCausedHostDeath = false;
    for (const SpecialMechanismAction& action : prepared) {
        if (action.kind != SpecialMechanismActionKind::ParasiteDamage
            && action.kind
                != SpecialMechanismActionKind::ParasiteKillTarget) {
            continue;
        }
        const WorldObjectInstance* target =
            findRuntimeObjectByIdIncludingInactive(
                state,
                action.targetObjectId
            );
        if (target == nullptr || !target->alive || target->health <= 0) {
            continue;
        }
        if (action.kind == SpecialMechanismActionKind::ParasiteDamage
            && runtimeDamageIsTypeImmune(state, *target,
                runtimeTypeImmuneSource(state, action.objectId), action.damage)) continue;
        bool lethal = action.kind
            == SpecialMechanismActionKind::ParasiteKillTarget;
        if (!lethal) {
            const int appliedDamage = runtimeAuthoredDamageAgainst(
                state,
                *target,
                action.damage,
                action.warheadId,
                0.0,
                1.0
            );
            lethal = appliedDamage >= target->health;
        }
        const auto link = parasiteDeathCandidate.parasiteLinks().find(
            action.targetObjectId
        );
        if (!lethal
            || link == parasiteDeathCandidate.parasiteLinks().end()) {
            continue;
        }
        const WorldObjectInstance* source =
            findRuntimeObjectByIdIncludingInactive(
                state,
                link->second.sourceObjectId
            );
        const RuntimeCellPoint targetCell =
            runtimeCellPointForObject(*target);
        const std::optional<RuntimeCellPoint> ejectCell = source == nullptr
            ? std::nullopt
            : runtimeParasitePlacementCell(
                state,
                *source,
                {targetCell.x, targetCell.y},
                nullptr,
                target->instanceId
            );
        ParasiteHostEventRequest hostDeath;
        hostDeath.currentTick = state.tick;
        hostDeath.targetObjectId = action.targetObjectId;
        hostDeath.event = ParasiteHostEventKind::DestroyedByParasite;
        hostDeath.ejectCellAvailable = ejectCell.has_value();
        if (ejectCell.has_value()) {
            hostDeath.ejectCell = {ejectCell->x, ejectCell->y};
        }
        const ParasiteOperationResult handled =
            parasiteDeathCandidate.handleParasiteHostEvent(
                hostDeath,
                SpecialEventSink{}
            );
        if (!handled.accepted) {
            result.accepted = false;
            state.simulationStatus =
                "parasite-caused host death rejected atomically";
            return result;
        }
        parasiteDeathActions.insert(
            parasiteDeathActions.end(),
            handled.actions.begin(),
            handled.actions.end()
        );
        hasParasiteCausedHostDeath = true;
    }
    if (hasParasiteCausedHostDeath) {
        parasiteDeathActions = prepareRuntimeParasiteActions(
            state,
            parasiteDeathActions
        );
        std::vector<SpecialMechanismAction> combined = prepared;
        combined.insert(
            combined.end(),
            parasiteDeathActions.begin(),
            parasiteDeathActions.end()
        );
        if (!validateRuntimeParasiteActionBatch(state, combined)) {
            result.accepted = false;
            state.simulationStatus =
                "parasite death/eject batch rejected atomically";
            return result;
        }
        state.specialMechanisms = std::move(parasiteDeathCandidate);
        const RuntimeParasiteActionApplyResult ejected =
            applyRuntimeParasiteActionsAtomically(
                state,
                parasiteDeathActions,
                inheritedDeathWeaponCause,
                fallbackSourceObjectId,
                suppressUnitDeathSounds
            );
        if (!ejected.accepted) {
            result.accepted = false;
            return result;
        }
        result.changed = ejected.changed;
    }

    for (const SpecialMechanismAction& action : prepared) {
        if (action.kind == SpecialMechanismActionKind::TimedBombDetonate) {
            continue;
        }
        WorldObjectInstance* source =
            findMutableRuntimeObjectByIdIncludingInactive(
                state,
                action.objectId
            );
        WorldObjectInstance* target =
            findMutableRuntimeObjectByIdIncludingInactive(
                state,
                action.targetObjectId
            );
        switch (action.kind) {
        case SpecialMechanismActionKind::ParasiteEnterLimbo: {
            const bool wasSelected = selectionContains(
                state,
                action.objectId
            );
            state.parasiteReselectAfterLimboObjectIds.erase(
                action.objectId
            );
            if (wasSelected
                && runtimeObjectReselectsAfterParasiteLimbo(
                    state,
                    *source
                )) {
                state.parasiteReselectAfterLimboObjectIds.insert(
                    action.objectId
                );
            }
            removeRuntimeSelection(state, action.objectId);
            if (!runtimeObjectRejoinsTeamAfterParasiteLimbo(
                    state,
                    *source
                )) {
                detachRuntimeObjectFromTeamMembership(
                    state,
                    action.objectId
                );
            }
            eraseRuntimeBehavior(state, action.objectId);
            clearRuntimeRenderPosition(*source);
            source->alive = false;
            source->mission = "ParasiteLimbo";
            source->subCell = -1;
            source->dockedToObjectId = -1;
            source->dockIndex = -1;
            result.changed = true;
            break;
        }
        case SpecialMechanismActionKind::ParasiteAttachHost:
            eraseRuntimeBehavior(state, action.targetObjectId);
            cancelRuntimePendingOccupyForTarget(
                state,
                action.targetObjectId,
                "parasite attached"
            );
            target->mission = action.paralyzes > 0
                ? "ParasiteParalyzed"
                : target->mission;
            result.changed = true;
            break;
        case SpecialMechanismActionKind::ParasiteReturnFromLimbo:
        case SpecialMechanismActionKind::ParasiteEject: {
            const bool reselect =
                state.parasiteReselectAfterLimboObjectIds.erase(
                    action.objectId
                ) > 0;
            activateRuntimeWorldObject(state, *source);
            source->health = std::max(1, source->health);
            setRuntimeObjectCell(
                state,
                *source,
                {action.cell.x, action.cell.y},
                false
            );
            source->mission = action.stunTicks > 0
                ? "ParasiteStunned"
                : "Guard";
            assignRuntimeInfantrySubcellAtCurrentLocation(state, *source);
            if (action.replacesTerminatingHost && target != nullptr
                && action.cell.x == target->x && action.cell.y == target->y) {
                // The host has not been drained yet. Preserve its last visible
                // feet, including subcells and an in-progress slope/bridge pose.
                const auto hostSubcell = worldObjectSubCellOffset(*target);
                const auto sourceSubcell = worldObjectSubCellOffset(*source);
                const bool hostHasContinuousLevel = target->renderMoveActive
                    || worldObjectHasStoppedGroundPose(*target);
                source->level = target->level;
                source->onBridge = target->onBridge;
                source->hasRenderPosition = true;
                source->renderMoveActive = false;
                source->renderX = runtimeObjectRenderX(*target)
                    + hostSubcell.x - sourceSubcell.x;
                source->renderY = runtimeObjectRenderY(*target)
                    + hostSubcell.y - sourceSubcell.y;
                source->renderLevel = runtimeDrawLevelForObject(*target);
                source->renderOnBridge = hostHasContinuousLevel
                    ? target->renderOnBridge : target->onBridge;
            }
            if (action.stunTicks > 0) {
                state.parasiteStunUntilTickByObject[source->instanceId] =
                    std::max(
                        state.tick,
                        action.tick
                    ) + action.stunTicks;
            } else {
                state.parasiteStunUntilTickByObject.erase(
                    source->instanceId
                );
            }
            if (reselect
                && runtimeOwnersAreAllied(
                    state,
                    state.economy.owner,
                    source->owner
                )) {
                addSelectedObject(state, source->instanceId);
            }
            if (action.kind == SpecialMechanismActionKind::ParasiteEject
                && target != nullptr && target->alive
                && target->mission == "ParasiteParalyzed") {
                target->mission = "Guard";
            }
            result.changed = true;
            break;
        }
        case SpecialMechanismActionKind::ParasiteDamage:
            if (target != nullptr && !runtimeDamageIsTypeImmune(state, *target,
                    runtimeTypeImmuneSource(state, action.objectId), action.damage)) {
                const int appliedDamage = runtimeAuthoredDamageAgainst(
                    state,
                    *target,
                    action.damage,
                    action.warheadId,
                    0.0,
                    1.0
                );
                target->health = std::max(0, target->health - appliedDamage);
                result.changed = appliedDamage > 0 || result.changed;
                if (target->health <= 0) {
                    RuntimeObjectExitRequest targetExit;
                    targetExit.objectId = target->instanceId;
                    targetExit.kind = RuntimeObjectExitKind::Destroyed;
                    targetExit.tick = action.tick;
                    targetExit.mission = "Destroyed";
                    targetExit.reason = action.reason.empty()
                        ? "parasite bite was lethal"
                        : action.reason;
                    targetExit.sourceObjectId = action.objectId;
                    targetExit.sourceOwner = action.owner;
                    targetExit.deathWeaponCause =
                        inheritedDeathWeaponCause;
                    (void)queueRuntimeObjectExit(
                        state,
                        std::move(targetExit)
                    );
                }
            }
            break;
        case SpecialMechanismActionKind::ParasiteKillTarget:
        case SpecialMechanismActionKind::ParasiteKillParasite: {
            const bool killSource = action.kind
                == SpecialMechanismActionKind::ParasiteKillParasite;
            if (killSource) {
                state.parasiteReselectAfterLimboObjectIds.erase(
                    action.objectId
                );
            }
            if (killSource && target != nullptr && target->alive
                && target->mission == "ParasiteParalyzed") {
                target->mission = "Guard";
                result.changed = true;
            }
            const int objectId = killSource
                ? action.objectId
                : action.targetObjectId;
            const WorldObjectInstance* killed =
                findRuntimeObjectByIdIncludingInactive(state, objectId);
            if (killed == nullptr || killed->health <= 0) {
                break;
            }
            RuntimeObjectExitRequest dependentExit;
            dependentExit.objectId = objectId;
            dependentExit.kind = RuntimeObjectExitKind::Destroyed;
            dependentExit.tick = action.tick;
            dependentExit.mission = "Destroyed";
            dependentExit.reason = action.reason;
            dependentExit.sourceObjectId = fallbackSourceObjectId >= 0
                ? fallbackSourceObjectId
                : action.objectId;
            dependentExit.sourceOwner = action.owner;
            dependentExit.deathWeaponCause = inheritedDeathWeaponCause;
            dependentExit.suppressDeathWeapon =
                action.suppressDeathWeapon;
            dependentExit.unitDeathSoundHandled = suppressUnitDeathSounds;
            if (killSource && action.sourceWasAttachedToHost
                && target != nullptr) {
                // Attached parasites retain their old launch cell in limbo.
                // Capture the current host before its exit clears that pose.
                dependentExit.unitDeathSoundPosition =
                    runtimeSpatialAudioPositionForObject(*target);
            } else if (killSource && !action.sourceWasAttachedToHost) {
                const auto actor = std::find_if(state.projectiles.begin(), state.projectiles.end(),
                    [&](const RuntimeProjectileEffect& projectile) {
                        return projectile.actorId == action.projectileId
                            && projectile.attackerObjectId == objectId
                            && runtimeProjectileIsParasiteLeap(state, projectile);
                    });
                if (actor != state.projectiles.end()) {
                    dependentExit.unitDeathSoundPosition = RuntimeSpatialAudioPosition{
                        actor->currentX, actor->currentY, actor->currentZ};
                }
            }
            result.changed = queueRuntimeObjectExit(
                state,
                std::move(dependentExit)
            ) == RuntimeObjectExitQueueResult::Queued || result.changed;
            break;
        }
        case SpecialMechanismActionKind::ParasiteReleaseTarget:
            if (target != nullptr && target->alive
                && target->mission == "ParasiteParalyzed") {
                target->mission = "Guard";
                result.changed = true;
            }
            break;
        case SpecialMechanismActionKind::TimedBombDetonate:
            break;
        }
    }
    if (result.changed) {
        invalidateRuntimeTickOccupancyCache(state);
        invalidateRuntimeObjectProjectionCache(state);
        state.navigationRefreshPending = true;
    }
    return result;
}

int runtimeParasiteRuleFramesToTicks(int frames, int fixedTickHz) {
    return std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(std::max(0, frames))
                * static_cast<double>(std::max(1, fixedTickHz))
                / 15.0
        ))
    );
}

bool launchRuntimeParasiteProjectile(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& target,
    const WeaponType& weapon,
    const RuntimeWeaponProfile& weaponProfile) {
    ensureRuntimeSpecialMechanismsInitialized(state);
    const WarheadType* warhead = weaponProfile.warhead;
    const UnitType* sourceType = findRuntimeUnitType(state.rules, source);
    const UnitType* targetType = findRuntimeUnitType(state.rules, target);
    const ProjectileType* projectileType =
        findRuntimeProjectileType(state.rules, weapon.projectile);
    const bool validTargetKind = target.kind == WorldObjectKind::Infantry
        || (target.kind == WorldObjectKind::Unit
            && !target.authoredAircraft && !target.airborne);
    if (warhead == nullptr || sourceType == nullptr || targetType == nullptr
        || !runtimeParasiteWarheadIsExclusive(weapon, *warhead)
        || projectileType == nullptr
        || runtimeSpawnedMissileProfileForWeapon(
            state,
            source,
            weapon
        ) != nullptr
        || projectileType->airburst || projectileType->shrapnelCount > 0
        || !trimAsciiLocal(projectileType->airburstWeapon).empty()
        || !trimAsciiLocal(projectileType->shrapnelWeapon).empty()
        || !source.alive || source.health <= 0
        || !target.alive || target.health <= 0
        || source.instanceId == target.instanceId
        || !validTargetKind
        || (target.kind == WorldObjectKind::Unit
            && !targetType->parasiteable)
        || runtimeParasiteObjectHasExclusiveAuthority(state, source)
        || runtimeParasiteObjectHasExclusiveAuthority(state, target, true)
        || !runtimeWeaponCanTargetObject(state, weapon, target)
        || (state.superWeaponsInitialized
            && state.superWeapons.isObjectProtected(
                target.instanceId,
                state.tick
            ))) {
        state.simulationStatus = "parasite launch rejected by host";
        return false;
    }

    const int previousNextActorId = state.nextProjectileActorId;
    const int projectileActorId = nextRuntimeProjectileActorId(state);
    ParasiteLaunchRequest request;
    request.projectileId = projectileActorId;
    request.currentTick = state.tick;
    request.sourceObjectId = source.instanceId;
    request.targetObjectId = target.instanceId;
    request.sourceOwner = source.owner;
    request.sourceTypeId = source.type;
    request.targetTypeId = target.type;
    request.weaponId = weapon.id;
    request.warheadId = warhead->id;
    request.targetKind = target.kind == WorldObjectKind::Infantry
        ? ParasiteTargetKind::Infantry
        : ParasiteTargetKind::Vehicle;
    const RuntimeCellPoint targetCell = runtimeCellPointForObject(target);
    request.targetCell = {targetCell.x, targetCell.y};
    request.damage = std::max(0, weapon.damage);
    request.sourceOrganic = sourceType->organic;
    request.sourceElite = source.veterancy >= 2;
    request.damageIntervalTicks = runtimeParasiteRuleFramesToTicks(
        request.sourceOrganic ? 36 : std::max(1, weapon.rof),
        state.config.fixedTickHz
    );
    request.paralyzes = std::max(0, warhead->paralyzes);
    request.targetMaximumHealth = std::max(1, targetType->strength);
    request.targetParasiteable = target.kind == WorldObjectKind::Infantry
        || targetType->parasiteable;
    request.culling = warhead->culling;
    request.normalCullingThresholdPermyriad =
        static_cast<int>(std::lround(
            std::clamp(
                state.specialMechanisms.rules().conditionRed,
                0.0,
                1.0
            ) * 10000.0
        ));
    request.eliteCullingThresholdPermyriad =
        static_cast<int>(std::lround(
            std::clamp(
                state.specialMechanisms.rules().conditionYellow,
                0.0,
                1.0
            ) * 10000.0
        ));

    RuntimeSpecialMechanismState specialCandidate =
        state.specialMechanisms;
    const ParasiteOperationResult began = specialCandidate.beginParasiteLaunch(
        request,
        SpecialEventSink{}
    );
    if (!began.accepted || began.idempotent || began.actions.size() != 1U
        || !validateRuntimeParasiteActionBatch(
            state,
            prepareRuntimeParasiteActions(state, began.actions)
        )) {
        state.nextProjectileActorId = previousNextActorId;
        state.simulationStatus = began.reason.empty()
            ? "parasite launch action rejected atomically"
            : "parasite launch rejected: " + began.reason;
        return false;
    }

    spawnRuntimeProjectile(
        state,
        source,
        target,
        weapon,
        weaponProfile,
        request.damage,
        -1,
        0,
        false,
        -1,
        -1.0,
        0,
        0,
        0,
        true,
        nullptr,
        projectileActorId
    );
    state.specialMechanisms = std::move(specialCandidate);
    const RuntimeParasiteActionApplyResult applied =
        applyRuntimeParasiteActionsAtomically(
            state,
            began.actions,
            {},
            source.instanceId
        );
    if (!applied.accepted) {
        state.simulationStatus =
            "parasite launch commit rejected unexpectedly";
        return false;
    }
    state.simulationStatus = "parasite launched #"
        + std::to_string(source.instanceId)
        + " -> #" + std::to_string(target.instanceId);
    return true;
}

bool applyRuntimeParasiteProjectileImpactExclusive(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile) {
    const auto pendingIt =
        state.specialMechanisms.pendingParasiteLaunches().find(
            projectile.actorId
        );
    if (pendingIt
        == state.specialMechanisms.pendingParasiteLaunches().end()) {
        return false;
    }
    const ParasitePendingLaunchState pending = pendingIt->second;
    const WorldObjectInstance* source =
        findRuntimeObjectByIdIncludingInactive(
            state,
            pending.sourceObjectId
        );
    const WorldObjectInstance* target = findObjectById(
        state,
        projectile.targetObjectId
    );
    const WarheadType* warhead =
        findRuntimeWarheadType(state.rules, projectile.warheadId);
    const WeaponType* weapon =
        findRuntimeWeaponType(state.rules, projectile.weaponId);
    const UnitType* targetType = target == nullptr
        ? nullptr
        : findRuntimeUnitType(state.rules, *target);
    const bool targetKindMatches = target != nullptr
        && ((pending.targetKind == ParasiteTargetKind::Infantry
                && target->kind == WorldObjectKind::Infantry)
            || (pending.targetKind == ParasiteTargetKind::Vehicle
                && target->kind == WorldObjectKind::Unit
                && !target->authoredAircraft && !target->airborne));
    const bool causalIdentityMatches = source != nullptr
        && source->health > 0 && !source->alive
        && source->mission == "ParasiteLimbo"
        && source->type == pending.sourceTypeId
        && projectile.attackerObjectId == pending.sourceObjectId
        && projectile.targetObjectId == pending.targetObjectId
        && projectile.owner == pending.owner
        && projectile.weaponId == pending.weaponId
        && projectile.warheadId == pending.warheadId;
    const bool strictWeapon = weapon != nullptr && warhead != nullptr
        && runtimeParasiteWarheadIsExclusive(*weapon, *warhead);
    const bool legalHit = causalIdentityMatches && strictWeapon
        && target != nullptr && target->alive && target->health > 0
        && target->type == pending.targetTypeId && targetKindMatches
        && targetType != nullptr
        && (pending.targetKind == ParasiteTargetKind::Infantry
            || targetType->parasiteable)
        && runtimeProjectileCanHitTarget(state, projectile, *target)
        && runtimeWarheadArmorMultiplier(
            warhead,
            runtimeTargetArmor(state, *target)
        ) > 0.0
        && !runtimeParasiteObjectHasExclusiveAuthority(state, *target, true)
        && (!state.superWeaponsInitialized
            || !state.superWeapons.isObjectProtected(
                target->instanceId,
                state.tick
            ));

    const RuntimeCellPoint impactCell = target != nullptr && target->alive
        ? runtimeCellPointForObject(*target)
        : projectile.toCell;
    const std::optional<RuntimeCellPoint> returnCell = source == nullptr
        ? std::nullopt
        : runtimeParasitePlacementCell(
            state,
            *source,
            {impactCell.x, impactCell.y},
            nullptr,
            legalHit && pending.targetKind == ParasiteTargetKind::Infantry
                ? target->instanceId : -1
        );
    ParasiteImpactRequest request;
    request.projectileId = projectile.actorId;
    request.currentTick = state.tick;
    request.impact = legalHit
        ? ParasiteImpactKind::Hit
        : ParasiteImpactKind::Miss;
    request.targetObjectId = target == nullptr
        ? pending.targetObjectId
        : target->instanceId;
    request.targetTypeId = target == nullptr
        ? pending.targetTypeId
        : target->type;
    request.targetKind = pending.targetKind;
    request.targetPresent = target != nullptr;
    request.targetAlive = target != nullptr && target->alive
        && target->health > 0;
    request.targetParasiteable = targetType != nullptr
        && (target->kind == WorldObjectKind::Infantry
            || targetType->parasiteable);
    request.targetMaximumHealth = targetType == nullptr
        ? std::max(1, pending.targetMaximumHealth)
        : std::max(1, targetType->strength);
    request.impactCell = {impactCell.x, impactCell.y};
    request.returnCellAvailable = returnCell.has_value();
    if (returnCell.has_value()) {
        request.returnCell = {returnCell->x, returnCell->y};
    }

    RuntimeSpecialMechanismState specialCandidate =
        state.specialMechanisms;
    const ParasiteOperationResult resolved =
        specialCandidate.resolveParasiteImpact(
            request,
            SpecialEventSink{}
        );
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(state, resolved.actions);
    if (!resolved.accepted
        || !validateRuntimeParasiteActionBatch(state, prepared)) {
        state.simulationStatus = resolved.reason.empty()
            ? "parasite impact action rejected atomically"
            : "parasite impact rejected: " + resolved.reason;
        return true;
    }

    state.specialMechanisms = std::move(specialCandidate);
    const RuntimeParasiteActionApplyResult applied =
        applyRuntimeParasiteActionsAtomically(
            state,
            prepared,
            runtimeDeathWeaponCauseForProjectile(projectile),
            pending.sourceObjectId
        );
    if (!applied.accepted) {
        state.simulationStatus =
            "parasite impact commit rejected unexpectedly";
        return true;
    }
    RuntimeProjectileEffect relationshipImpact = projectile;
    relationshipImpact.impactAnimId.clear();
    relationshipImpact.particleId.clear();
    relationshipImpact.damage = 0;
    relationshipImpact.cellSpread = 0.0;
    relationshipImpact.bright = false;
    relationshipImpact.reportSoundId.clear();
    recordRuntimeProjectileReplayEvent(
        state,
        relationshipImpact,
        "ProjectileImpact"
    );
    ++state.actionSummary.impactsThisTick;
    (void)drainRuntimeObjectExitQueue(state);
    state.simulationStatus = legalHit
        ? "parasite impact accepted"
        : "parasite impact resolved as miss";
    return true;
}

bool handleRuntimeParasiteHostEvent(
    RuntimeProductionHostState& state,
    int targetObjectId,
    ParasiteHostEventKind event,
    int stunTicks) {
    ensureRuntimeSpecialMechanismsInitialized(state);
    const auto link = state.specialMechanisms.parasiteLinks().find(
        targetObjectId
    );
    if (link == state.specialMechanisms.parasiteLinks().end()) {
        return true;
    }
    const int sourceObjectId = link->second.sourceObjectId;
    const WorldObjectInstance* target =
        findRuntimeObjectByIdIncludingInactive(state, targetObjectId);
    const WorldObjectInstance* source =
        findRuntimeObjectByIdIncludingInactive(
            state,
            sourceObjectId
        );
    const RuntimeCellPoint targetCell = target == nullptr
        ? RuntimeCellPoint{}
        : runtimeCellPointForObject(*target);
    const std::optional<RuntimeCellPoint> ejectCell = source == nullptr
        ? std::nullopt
        : runtimeParasitePlacementCell(
            state,
            *source,
            {targetCell.x, targetCell.y}
        );
    ParasiteHostEventRequest request;
    request.currentTick = state.tick;
    request.targetObjectId = targetObjectId;
    request.event = event;
    request.ejectCellAvailable = ejectCell.has_value();
    if (ejectCell.has_value()) {
        request.ejectCell = {ejectCell->x, ejectCell->y};
    }
    request.stunTicks = std::max(0, stunTicks);

    RuntimeSpecialMechanismState specialCandidate =
        state.specialMechanisms;
    const ParasiteOperationResult handled =
        specialCandidate.handleParasiteHostEvent(
            request,
            SpecialEventSink{}
        );
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(state, handled.actions);
    if (!handled.accepted
        || !validateRuntimeParasiteActionBatch(state, prepared)) {
        state.simulationStatus = handled.reason.empty()
            ? "parasite host event rejected atomically"
            : "parasite host event rejected: " + handled.reason;
        return false;
    }
    state.specialMechanisms = std::move(specialCandidate);
    const RuntimeParasiteActionApplyResult applied =
        applyRuntimeParasiteActionsAtomically(
            state,
            prepared,
            {},
            sourceObjectId
        );
    if (!applied.accepted) {
        state.simulationStatus =
            "parasite host event commit rejected unexpectedly";
        return false;
    }
    return true;
}

bool preflightRuntimeParasiteHostEvent(
    RuntimeGameSessionState& state,
    int targetObjectId,
    ParasiteHostEventKind event,
    int stunTicks) {
    ensureRuntimeSpecialMechanismsInitialized(state);
    const auto link = state.specialMechanisms.parasiteLinks().find(
        targetObjectId
    );
    if (link == state.specialMechanisms.parasiteLinks().end()) {
        return true;
    }
    const int sourceObjectId = link->second.sourceObjectId;
    const WorldObjectInstance* target =
        findRuntimeObjectByIdIncludingInactive(state, targetObjectId);
    const WorldObjectInstance* source =
        findRuntimeObjectByIdIncludingInactive(state, sourceObjectId);
    const RuntimeCellPoint targetCell = target == nullptr
        ? RuntimeCellPoint{}
        : runtimeCellPointForObject(*target);
    const std::optional<RuntimeCellPoint> ejectCell = source == nullptr
        ? std::nullopt
        : runtimeParasitePlacementCell(
            state,
            *source,
            {targetCell.x, targetCell.y}
        );
    ParasiteHostEventRequest request;
    request.currentTick = state.tick;
    request.targetObjectId = targetObjectId;
    request.event = event;
    request.ejectCellAvailable = ejectCell.has_value();
    if (ejectCell.has_value()) {
        request.ejectCell = {ejectCell->x, ejectCell->y};
    }
    request.stunTicks = std::max(0, stunTicks);

    RuntimeSpecialMechanismState specialCandidate =
        state.specialMechanisms;
    const ParasiteOperationResult handled =
        specialCandidate.handleParasiteHostEvent(
            request,
            SpecialEventSink{}
        );
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(state, handled.actions);
    return handled.accepted
        && validateRuntimeParasiteActionBatch(state, prepared);
}

bool requestRuntimeChronoMoveWithParasite(
    RuntimeProductionHostState& state,
    int objectId,
    SpecialCell destination,
    int destinationLevel,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied,
    std::string& reason) {
    if (runtimeObjectManagedByAirstrike(state, objectId)) {
        reason = "chrono move rejected: Airstrike owns plane";
        return false;
    }
    if (runtimeObjectSuspendedByMagBeam(state, objectId)) {
        reason = "chrono move rejected: mag beam owns locomotor";
        return false;
    }
    const auto* mover = findObjectById(state, objectId);
    if (mover != nullptr && !runtimeObjectAllowsMoveToCell(state, *mover, {destination.x, destination.y})) {
        reason = "chrono move rejected: destination is shrouded for the unit's owner";
        return false;
    }
    ensureRuntimeSpecialMechanismsInitialized(state);
    RuntimeSpecialMechanismState specialCandidate =
        state.specialMechanisms;
    std::vector<WorldObjectInstance> objectCandidate =
        state.world.getObjects();
    if (!specialCandidate.requestChronoMove(
            objectCandidate,
            state.rules,
            objectId,
            destination,
            destinationLevel,
            state.tick,
            cellIsLegal,
            cellIsOccupied,
            SpecialEventSink{},
            reason,
            state.config.fixedTickHz
        )) {
        return false;
    }

    std::vector<SpecialMechanismAction> parasiteActions;
    const auto link = specialCandidate.parasiteLinks().find(objectId);
    if (link != specialCandidate.parasiteLinks().end()) {
        const WorldObjectInstance* target = findObjectById(state, objectId);
        const WorldObjectInstance* source =
            findRuntimeObjectByIdIncludingInactive(
                state,
                link->second.sourceObjectId
            );
        const RuntimeCellPoint targetCell = target == nullptr
            ? RuntimeCellPoint{}
            : runtimeCellPointForObject(*target);
        const std::optional<RuntimeCellPoint> ejectCell = source == nullptr
            ? std::nullopt
            : runtimeParasitePlacementCell(
                state,
                *source,
                {targetCell.x, targetCell.y}
            );
        ParasiteHostEventRequest teleporting;
        teleporting.currentTick = state.tick;
        teleporting.targetObjectId = objectId;
        teleporting.event = ParasiteHostEventKind::Teleporting;
        teleporting.ejectCellAvailable = ejectCell.has_value();
        if (ejectCell.has_value()) {
            teleporting.ejectCell = {ejectCell->x, ejectCell->y};
        }
        teleporting.stunTicks = std::max(1, state.config.fixedTickHz);
        const ParasiteOperationResult handled =
            specialCandidate.handleParasiteHostEvent(
                teleporting,
                SpecialEventSink{}
            );
        parasiteActions = prepareRuntimeParasiteActions(
            state,
            handled.actions
        );
        if (!handled.accepted
            || !validateRuntimeParasiteActionBatch(
                state,
                parasiteActions
            )) {
            reason = handled.reason.empty()
                ? "parasite teleport event rejected atomically"
                : handled.reason;
            return false;
        }
    }

    const auto shadow = std::find_if(
        objectCandidate.begin(),
        objectCandidate.end(),
        [objectId](const WorldObjectInstance& object) {
            return object.instanceId == objectId;
        }
    );
    WorldObjectInstance* live = findMutableObjectById(state, objectId);
    if (shadow == objectCandidate.end() || live == nullptr || !live->alive) {
        reason = "chrono object disappeared before commit";
        return false;
    }
    state.specialMechanisms = std::move(specialCandidate);
    live->mission = shadow->mission;
    const RuntimeParasiteActionApplyResult applied =
        applyRuntimeParasiteActionsAtomically(
            state,
            parasiteActions,
            {},
            objectId
        );
    if (!applied.accepted) {
        reason = "parasite teleport action commit rejected";
        return false;
    }
    if (auto* moved = findMutableObjectById(state, objectId)) {
        runtimeSpecialEventSink(state)({"ChronoWarpOut", state.tick, objectId, -1, 0,
            {moved->x, moved->y}, reason});
        clearRuntimeRenderPosition(*moved);
    }
    return true;
}

bool runtimeChronoCellIsOccupied(
    const RuntimeGameSessionState& state, const std::unordered_map<int,int>& occupied,
    int x, int y, int objectId) {
    const auto* object=findObjectById(state,objectId);
    if(object && runtimeInfantryUsesGroundSubcells(*object))
        return runtimeAvailableInfantrySubcells(state,*object,{x,y}).empty();
    const auto found=occupied.find(runtimeCellKey(x,y));
    return found!=occupied.end() && found->second!=objectId;
}

void spawnRuntimeChronoWarpEffect(
    RuntimeProductionHostState& state, const SpecialMechanismEvent& event, bool playSound) {
    WorldObjectInstance* object = findMutableObjectById(state, event.objectId);
    if (object == nullptr || !object->alive) return;
    const bool reinforcement = event.kind == "ChronoReinforcementIn";
    const bool arriving = reinforcement || event.kind == "ChronoWarpIn";
    const auto boarding = arriving && !reinforcement
        ? runtimeChronoTransportDestinationAdmission(state, *object, runtimeCellPointForObject(*object))
        : std::optional<bool>{};
    const auto pendingHost = state.pendingTransportTargets.find(object->instanceId);
    const auto* boardingHost = boarding && *boarding && pendingHost != state.pendingTransportTargets.end()
        ? findObjectById(state, pendingHost->second) : nullptr;
    if (arriving) {
        // The standalone mechanism uses a packed 512-wide cell identifier;
        // runtime occupancy and saves use the canonical runtime cell key.
        object->cell = runtimeCellKey(object->x, object->y);
        object->onBridge = runtimeCellIsBridge(state, object->x, object->y);
        if(runtimeInfantryUsesGroundSubcells(*object)) {
            releaseRuntimeInfantrySubcellReservation(state,object->instanceId);
            if (boardingHost) object->subCell = -1;
            else assignRuntimeInfantrySubcellAtCurrentLocation(state,*object);
        }
        clearRuntimeRenderPosition(*object);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        invalidateRuntimeObjectProjectionCache(state);
    }
    const auto& rules = state.rules.getSpecialMechanismRules();
    RuntimeImpactEffect effect;
    effect.attackerObjectId = object->instanceId;
    effect.targetObjectId = object->instanceId;
    effect.owner = object->owner;
    effect.cell = {object->x, object->y};
    effect.level = object->level;
    effect.hasWorldPosition = true;
    const auto offset = worldObjectSubCellOffset(*object);
    effect.worldX = (object->hasRenderPosition ? object->renderX : object->x) + offset.x;
    effect.worldY = (object->hasRenderPosition ? object->renderY : object->y) + offset.y;
    effect.worldLevel = object->hasRenderPosition ? object->renderLevel : object->level;
    if (boardingHost) {
        const auto hull = runtimeTransportMotionBody(*boardingHost);
        effect.worldX = hull.x; effect.worldY = hull.y; effect.worldLevel = hull.level;
    }
    effect.facing = object->facing;
    effect.lifecycleState = reinforcement ? "ChronoReinforcementIn"
        : arriving ? "ChronoWarpIn" : "ChronoWarpOut";
    effect.renderIntent = "chronoWarp";
    // Retail TeleportLocomotionClass uses Rules::WarpOut at BOTH ends.
    // WarpIn is a separate, red animation used by chrono reinforcements.
    effect.animId = reinforcement ? rules.warpInAnim : rules.warpOutAnim;
    if (!effect.animId.empty() && toLowerAsciiLocal(effect.animId) != "none") {
        initializeRuntimeImpactAnimationTimeline(state, effect, true);
        state.impactEffects.push_back(std::move(effect));
    }
    if (playSound) {
        const UnitType* unit = findRuntimeUnitType(state.rules, *object);
        std::string sound = unit == nullptr ? std::string()
            : (arriving ? unit->chronoInSound : unit->chronoOutSound);
        if (trimAsciiLocal(sound).empty()) sound = arriving ? rules.chronoInSound : rules.chronoOutSound;
        if (!trimAsciiLocal(sound).empty()) emitRuntimeSound(state, "sound:" + trimAsciiLocal(sound), runtimeSpatialAudioPositionForObject(*object));
    }
}

bool holdRuntimeChronoRecovery(
    RuntimeGameSessionState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior) {
    if (!state.specialMechanismsInitialized
        || !state.specialMechanisms.isChronoMovementLocked(object.instanceId, state.tick)) return false;
    clearRuntimeRenderPosition(object);
    behavior.state = RuntimeBehaviorState::Cooldown;
    if (!state.specialMechanisms.isChronoInTransit(object.instanceId)) object.mission = "ChronoFrozen";
    return true;
}

std::optional<bool> advanceRuntimeTeleporterMove(
    RuntimeProductionHostState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, RuntimeCellPoint destination) {
    const UnitType* unit = findRuntimeUnitType(state.rules, object);
    // Harvesters manage their drive-to-ore / blink-to-refinery cycle separately.
    if (unit == nullptr || !unit->teleporter || unit->harvester) return std::nullopt;
    const auto transportAdmission = runtimeChronoTransportDestinationAdmission(state, object, destination);
    if (transportAdmission && !*transportAdmission) {
        behavior.state = RuntimeBehaviorState::Blocked;
        return false;
    }
    if (runtimeCellPointForObject(object) == destination && !transportAdmission) return std::nullopt;
    const auto generatedTerrain = state.terrainByCell.empty()
        ? buildRuntimeTerrainLookup(state.world) : std::unordered_map<int, const TerrainCell*>{};
    const auto& terrain = state.terrainByCell.empty() ? generatedTerrain : state.terrainByCell;
    const auto occupied = buildRuntimeOccupancyLookup(state);
    const SpecialCellPredicate legal = [&](int x, int y, int ignoredId) {
        if (const auto entry = runtimeChronoTransportDestinationAdmission(state, object, {x, y})) return *entry;
        return runtimeCellIsPassable(state, terrain, occupied, {x, y}, object, ignoredId);
    };
    const SpecialCellPredicate blocked = [&](int x, int y, int ignoredId) {
        if (const auto entry = runtimeChronoTransportDestinationAdmission(state, object, {x, y})) return !*entry;
        return runtimeChronoCellIsOccupied(state,occupied,x,y,ignoredId);
    };
    std::string reason;
    const auto pendingHost = state.pendingTransportTargets.find(object.instanceId);
    const auto* host = transportAdmission && pendingHost != state.pendingTransportTargets.end()
        ? findObjectById(state, pendingHost->second) : nullptr;
    const int destinationLevel = host ? host->level : terrainLevelAtRuntimeCell(state, destination.x, destination.y);
    if (!requestRuntimeChronoMoveWithParasite(state, object.instanceId,
            {destination.x, destination.y}, destinationLevel,
            legal, blocked, reason)) {
        behavior.state = RuntimeBehaviorState::Blocked;
        return false;
    }
    behavior.path.clear();
    behavior.pathRequestPending = false;
    behavior.moveCooldownTicks = 0;
    behavior.renderMoveTicksRemaining = 0;
    behavior.blockedTicks = 0;
    behavior.state = RuntimeBehaviorState::Cooldown;
    releaseRuntimeInfantrySubcellReservation(state, object.instanceId);
    clearRuntimeRenderPosition(object);
    return true;
}

bool advanceRuntimeTemporalAttacks(RuntimeProductionHostState& state) {
    if (state.specialMechanisms.temporalTargets().empty()) return false;
    // Work from IDs: completion and interruption mutate the relationship map.
    const auto targets = state.specialMechanisms.temporalTargets();
    const auto sink = runtimeSpecialEventSink(state);
    bool changed = false;
    bool playerObjectLost = false;
    for (const auto& [targetId, temporal] : targets) {
        const int previousTick = temporal.lastAdvancedTick < 0
            ? std::max(0, state.tick - 1) : temporal.lastAdvancedTick;
        if (!state.specialMechanisms.beginTemporalTick(targetId, state.tick)) continue;
        const auto frames = runtimeRuleFrameAtTick(state.tick, state.config.fixedTickHz)
            - runtimeRuleFrameAtTick(previousTick, state.config.fixedTickHz);
        for (const auto& [attackerId, unusedProgress] : temporal.progressByAttacker) {
            const auto* target = findObjectById(state, targetId);
            const auto* source = findRuntimeObjectByIdIncludingInactive(state, attackerId);
            const WorldObjectInstance* host = nullptr;
            const auto explicitOrder = state.behaviors.find(attackerId);
            const bool forceFire = explicitOrder != state.behaviors.end()
                && explicitOrder->second.kind == RuntimeBehaviorKind::Attack
                && explicitOrder->second.targetObjectId == targetId && explicitOrder->second.forceFire;
            bool valid = source && target && target->health > 0 && runtimeCombatCanAct(state, *source)
                && !runtimeCarrierAircraftHasDeckProtection(state, *target)
                && (forceFire || !runtimeOwnersAreAllied(state, source->owner, target->owner));
            if (valid && !source->alive) {
                const auto cursor = state.combatDetails.fireCursors.find(attackerId);
                if (cursor != state.combatDetails.fireCursors.end()
                    && cursor->second.lastTargetObjectId == targetId)
                    host = findObjectById(state, cursor->second.sourceAnchorObjectId);
                valid = host && runtimeCombatValidPassenger(*source, *host) && runtimeCombatCanAct(state, *host);
            } else if (valid) {
                const auto order = state.behaviors.find(attackerId);
                const auto cursor = state.combatDetails.fireCursors.find(attackerId);
                const auto* unit = findRuntimeUnitType(state.rules, *source);
                const bool independentMoveFire = unit && unit->opportunityFire && !unit->noMovingFire
                    && order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Move
                    && cursor != state.combatDetails.fireCursors.end()
                    && cursor->second.lastTargetObjectId == targetId;
                valid = order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack
                    && order->second.targetObjectId == targetId;
                valid = valid || independentMoveFire;
            }
            const auto hostOrder = host == nullptr ? state.behaviors.end()
                : state.behaviors.find(host->instanceId);
            const bool profileForceFire = forceFire
                || (hostOrder != state.behaviors.end()
                    && hostOrder->second.kind == RuntimeBehaviorKind::Attack
                    && hostOrder->second.targetObjectId == targetId
                    && hostOrder->second.forceFire);
            const auto profile = !valid ? RuntimeWeaponProfile{}
                : host ? runtimeCombatPassengerWeaponProfile(state, *source, target, profileForceFire)
                : runtimeWeaponProfileForObject(state, *source, target, profileForceFire);
            valid = valid && profile.weapon && profile.warhead && profile.warhead->temporal;
            if (valid) {
                const auto pose = runtimeCombatFiringPose(*source, host);
                const double range = host ? state.rules.getCombatDetailRules().openToppedWarpDistance
                    : runtimeEffectiveWeaponRange(state, pose, *target, *profile.weapon);
                const double distance = host
                    ? runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(*host), *target)
                    : runtimeWeaponDistanceToTarget(state, pose, *target, *profile.weapon);
                valid = distance <= range && distance + 0.001 >= profile.weapon->minimumRange
                    && runtimeObjectVisibleToOwner(state, source->owner, *target)
                    && runtimeWeaponHasLineOfSight(state, pose, *target, *profile.weapon);
            }
            if (!valid) {
                state.specialMechanisms.clearTemporalAttacker(attackerId, targetId, state.tick,
                    sink, "temporal source or target no longer available");
                changed = true;
                continue;
            }
            if (frames <= 0) continue;
            // Temporal ignores armor verses, difficulty health and ordinary
            // firepower bonuses. ElitePrimary supplies its own authored Damage.
            const int progress = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(), std::max(0, profile.weapon->damage) * frames));
            bool completed = false;
            std::string reason;
            changed = state.specialMechanisms.applyTemporalHit(state.world.accessObjects(), state.rules,
                attackerId, targetId, progress, temporal.requiredProgress, state.tick,
                [&state](const std::string& a, const std::string& b) { return runtimeOwnersAreAllied(state, a, b); },
                sink, completed, reason, forceFire) || changed;
            if (completed) {
                playerObjectLost = playerObjectLost || (target->kind != WorldObjectKind::Structure && target->owner == state.economy.owner);
                RuntimeObjectExitRequest exit;
                exit.objectId = targetId;
                exit.kind = RuntimeObjectExitKind::TemporalDestroyed;
                exit.tick = state.tick;
                exit.mission = "TemporalRemoved";
                exit.reason = "temporal continuous erasure complete";
                exit.sourceObjectId = attackerId;
                exit.sourceOwner = source->owner;
                exit.suppressDeathWeapon = true;
                queueRuntimeObjectExit(state, std::move(exit));
                break;
            }
        }
    }
    changed = drainRuntimeObjectExitQueue(state) || changed;
    if (playerObjectLost && state.tick - state.runtimeLastUnitLostSoundTick
            >= std::max(1, state.config.fixedTickHz * 2)) {
        emitRuntimeSound(state, runtimeSoundIntentUnitLost);
        state.runtimeLastUnitLostSoundTick = state.tick;
    }
    return changed;
}

void resolveRuntimeTemporalBeamEndpoints(RuntimeProductionHostState& state) {
    for (auto& snapshot : state.specialMechanismRenderSnapshots) {
        if (snapshot.kind != "temporalBeam") continue;
        const auto* source = findRuntimeObjectByIdIncludingInactive(state, snapshot.objectId);
        const auto* target = findObjectById(state, snapshot.targetObjectId);
        if (source == nullptr || target == nullptr) continue;
        const WorldObjectInstance* host = nullptr;
        if (!source->alive) {
            const auto cursor = state.combatDetails.fireCursors.find(source->instanceId);
            if (cursor != state.combatDetails.fireCursors.end())
                host = findObjectById(state, cursor->second.sourceAnchorObjectId);
            if (!host || !runtimeCombatValidPassenger(*source, *host)) continue;
        }
        const auto sourceOrder = state.behaviors.find(snapshot.objectId);
        const auto hostOrder = host == nullptr ? state.behaviors.end()
            : state.behaviors.find(host->instanceId);
        const bool profileForceFire = (sourceOrder != state.behaviors.end()
            && sourceOrder->second.kind == RuntimeBehaviorKind::Attack
            && sourceOrder->second.targetObjectId == snapshot.targetObjectId
            && sourceOrder->second.forceFire)
            || (hostOrder != state.behaviors.end()
                && hostOrder->second.kind == RuntimeBehaviorKind::Attack
                && hostOrder->second.targetObjectId == snapshot.targetObjectId
                && hostOrder->second.forceFire);
        auto profile = host ? runtimeCombatPassengerWeaponProfile(state, *source, target, profileForceFire)
            : runtimeWeaponProfileForObject(state, *source, target, profileForceFire);
        profile.openTransport = host != nullptr;
        if (profile.weapon == nullptr || !profile.weapon->radBeam
            || profile.warhead == nullptr || !profile.warhead->temporal) continue;
        const auto pose = runtimeCombatFiringPose(*source, host);
        auto muzzle = runtimeProjectileSourcePosition(state, pose, profile,
            runtimeWeaponFiringFacing(state, pose, *target));
        alignRuntimeWeaponMuzzleToSprite(state, pose, profile, runtimeWeaponFiringFacing(state, pose, *target), muzzle);
        const auto endpoint = runtimeProjectileTargetPosition(state, *target);
        snapshot.sourceX = muzzle.x;
        snapshot.sourceY = muzzle.y;
        snapshot.sourceLevel = muzzle.z;
        snapshot.sourceScreenOffsetX = muzzle.screenOffsetX;
        snapshot.sourceNativeFixedX = muzzle.nativeFixedX;
        snapshot.sourceScreenOffsetY = muzzle.screenOffsetY;
        snapshot.sourceNativeFixedY = muzzle.nativeFixedY;
        snapshot.targetX = endpoint.x;
        snapshot.targetY = endpoint.y;
        snapshot.targetLevel = endpoint.level;
        snapshot.color = state.rules.getAudioVisualValue("ChronoBeamColor");
        if (snapshot.color.empty()) snapshot.color = "128,200,255";
        snapshot.owner = source->owner;
        snapshot.hasBeamEndpoints = true;
    }
}

void spawnRuntimeTemporalEraseEffect(
    RuntimeProductionHostState& state, const SpecialMechanismEvent& event) {
    const auto* target = findObjectById(state, event.targetObjectId);
    if (target == nullptr) return;
    RuntimeImpactEffect effect;
    effect.attackerObjectId = event.objectId;
    effect.targetObjectId = target->instanceId;
    effect.owner = target->owner;
    effect.cell = {target->x, target->y};
    effect.level = target->level;
    effect.hasWorldPosition = true;
    const auto offset = worldObjectSubCellOffset(*target);
    effect.worldX = runtimeObjectRenderX(*target) + offset.x;
    effect.worldY = runtimeObjectRenderY(*target) + offset.y;
    effect.worldLevel = runtimeDrawLevelForObject(*target);
    effect.facing = target->facing;
    effect.lifecycleState = "TemporalErase";
    effect.renderIntent = "temporalErase";
    effect.animId = state.rules.getSpecialMechanismRules().warpAwayAnim;
    if (effect.animId.empty() || toLowerAsciiLocal(effect.animId) == "none") return;
    initializeRuntimeImpactAnimationTimeline(state, effect, true);
    if (const auto* art = state.art.findArtType(effect.animId)) {
        if (!art->reportSound.empty()) emitRuntimeSound(state, "sound:" + art->reportSound, runtimeSpatialAudioPositionForImpact(effect), true);
    }
    state.impactEffects.push_back(std::move(effect));
}

int runtimeCrewSide(const RuntimeGameSessionState& state, const std::string& owner) {
    const auto country = toUpperAsciiLocal(trimAsciiLocal(runtimeCountryForOwner(state, owner)));
    const auto& sides = state.rules.getSides();
    for (std::size_t side = 0; side < std::min<std::size_t>(3, sides.size()); ++side)
        for (const auto& member : sides[side].members)
            if (toUpperAsciiLocal(trimAsciiLocal(member)) == country) return static_cast<int>(side);
    return runtimeBasePlanningSideForCountry(country);
}

std::vector<RuntimeCrewSurvivorSpawn> prepareRuntimeCrewSurvivors(
    const RuntimeProductionHostState& state, const WorldObjectInstance& object,
    const RuntimeObjectExitRequest& request) {
    std::vector<RuntimeCrewSurvivorSpawn> result;
    const bool sold = request.kind == RuntimeObjectExitKind::Sold;
    const bool structure = object.kind == WorldObjectKind::Structure;
    if ((!sold && request.kind != RuntimeObjectExitKind::Destroyed) || request.suppressCrew
        || !object.alive || object.rubble || object.garrisonHostObjectId >= 0
        || state.multiplayerDefeatedOwners.contains(runtimeCanonicalHumanHouseOwner(object.owner))) return result;
    const auto* building = structure ? findRuntimeBuildingTypeById(state.rules, object.type) : nullptr;
    // The retail vehicle branch excludes transports (Passengers != 0),
    // aircraft, and Crewed=no. Passengers are separate world objects.
    const auto* vehicle = object.kind == WorldObjectKind::Unit && !object.authoredAircraft
        ? state.rules.findUnitType("UnitType", object.type) : nullptr;
    if (structure ? (!building || !building->crewed)
        : (sold || !vehicle || !vehicle->crewed || vehicle->passengers != 0)) return result;

    const auto& rules = state.rules.getCrewSurvivorRules();
    const int side = runtimeCrewSide(state, object.owner);
    int remaining = structure ? runtimeBuildingCrewCount(building, rules,
        state.rules.getRefundPercent(), side, object.hasBeenCaptured) : 1;
    if (remaining <= 0) return result;
    RuntimeCrewRandom random{runtimeCombatRandomMix(state.config.simulationRandomSeed)
        ^ runtimeCombatRandomMix(static_cast<std::uint32_t>(object.instanceId))
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(request.tick)) << 32)
        ^ 0x4352455753555256ULL};
    if (!structure && !random.chance(rules.escapeChance)) return result;
    auto cells = structure ? runtimeStructureFootprintCellsForObject(state, object)
        : std::vector<RuntimeCellPoint>{runtimeCellPointForObject(object)};
    if (cells.empty()) return result;
    const bool armed = building ? (!building->primaryWeapon.empty() || !building->secondaryWeapon.empty())
        : (!vehicle->primaryWeapon.empty() || !vehicle->secondaryWeapon.empty());
    const bool constructionYard = building && toUpperAsciiLocal(trimAsciiLocal(building->factory)) == "BUILDINGTYPE";
    const bool selected = !structure && std::find(state.selectedObjectIds.begin(), state.selectedObjectIds.end(),
        object.instanceId) != state.selectedObjectIds.end();
    bool engineerReturned = false;
    const auto append = [&](RuntimeCellPoint cell) {
        // Selling a construction yard may replace one crew member with an
        // engineer. The retail sale loop allows at most one; capture disables it.
        std::string type;
        if (constructionYard && !object.hasBeenCaptured && (!sold || !engineerReturned)
            && random.range(0, 99) < 25) type = rules.engineer;
        else type = side < 0 || (armed && random.range(0, 99) < 15)
            ? rules.technician : rules.crewTypes[side];
        const auto* infantry = state.rules.findUnitType("InfantryType", type);
        if (!infantry) return;
        const int maximum = std::max(1, infantry->strength);
        const int woundedMaximum = structure ? maximum : std::max(1, maximum / 2);
        const int health = sold ? maximum : random.range(std::min(5, woundedMaximum), woundedMaximum);
        RuntimeCellPoint offset = formationOffsetForIndex(random.range(1, 8), 1);
        result.push_back({object.owner, type, cell, {cell.x + offset.x * 2, cell.y + offset.y * 2}, health, selected});
        engineerReturned = engineerReturned || infantry->engineer;
        --remaining;
    };
    if (sold) {
        const int count = remaining;
        for (int i = 0; i < count; ++i) append(cells[random.range(0, static_cast<int>(cells.size()) - 1)]);
    } else if (structure) {
        // Building destruction rolls once per foundation cell, up to the
        // same crew limit. C4 and capture modify this independently of CrewEscape.
        const auto bomb = state.specialMechanisms.timedBombs().find(object.instanceId);
        const bool c4 = request.c4Explosion || (bomb != state.specialMechanisms.timedBombs().end()
            && bomb->second.kind == TimedBombKind::C4);
        const int upper = (c4 ? 1 : 2) + (object.hasBeenCaptured ? 6 : 0);
        for (const auto& cell : cells) {
            if (remaining <= 0) break;
            if (random.range(0, upper) == 1) append(cell);
        }
    } else append(cells.front());
    return result;
}

void spawnRuntimeCrewSurvivors(RuntimeProductionHostState& state,
    const std::vector<RuntimeCrewSurvivorSpawn>& survivors) {
    // This runs after the source has released its footprint and all pointer
    // users in object finalization have finished; additions can grow the vector.
    for (const auto& survivor : survivors) {
        WorldObjectInstance probe;
        probe.instanceId = nextRuntimeObjectInstanceId(state);
        probe.kind = WorldObjectKind::Infantry; probe.type = survivor.type;
        probe.owner = survivor.owner; probe.x = survivor.cell.x; probe.y = survivor.cell.y;
        const auto cell = findRuntimePassableCellNear(state, probe, survivor.cell, 0);
        if (!cell) continue;
        auto* infantry = addRuntimeObjectInstanceForOwner(state, survivor.owner,
            RuntimeBuildKind::Infantry, survivor.type, *cell, std::nullopt, false);
        if (!infantry) continue;
        infantry->health = survivor.health;
        state.cloakObservedHealthByObject[infantry->instanceId] = infantry->health;
        if (survivor.selected) state.selectedObjectIds.push_back(infantry->instanceId);
        const auto scatter = findRuntimePassableCellNear(state, *infantry, survivor.scatter, 1);
        if (scatter && *scatter != *cell) {
            RuntimeObjectBehavior behavior;
            behavior.kind = RuntimeBehaviorKind::Move;
            behavior.state = RuntimeBehaviorState::Queued;
            behavior.moveTarget = *scatter;
            state.behaviors[infantry->instanceId] = std::move(behavior);
        }
        invalidateRuntimeTickOccupancyCache(state);
        state.navigationRefreshPending = true;
    }
}

std::optional<RuntimeDeathCrateSpawn> prepareRuntimeDeathCrate(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectExitRequest& request) {
    if (request.kind != RuntimeObjectExitKind::Destroyed || !object.alive
        || object.rubble || object.garrisonHostObjectId >= 0) return std::nullopt;
    RuntimeDeathCrateSpawn drop{runtimeCellPointForObject(object), false, object.instanceId};
    if (object.kind == WorldObjectKind::Structure) {
        const auto* building = state.rules.findBuildingType(object.type);
        if (!building || !building->crateBeneath) return std::nullopt;
        drop.money = building->crateBeneathIsMoney;
        const auto footprint = runtimeStructureFootprintCellsForObject(state, object);
        if (!footprint.empty()) {
            int x = 0, y = 0;
            for (const auto& cell : footprint) { x += cell.x; y += cell.y; }
            drop.center = {x / static_cast<int>(footprint.size()), y / static_cast<int>(footprint.size())};
        }
    } else {
        const auto* unit = object.kind == WorldObjectKind::Unit && !object.authoredAircraft
            ? state.rules.findUnitType("UnitType", object.type) : nullptr;
        if (!unit || !unit->carriesCrate) return std::nullopt;
        if (unit->isTrain ? !state.triggers.trainCrate : !state.triggers.truckCrate) return std::nullopt;
    }
    return drop;
}

bool spawnRuntimeDeathCrate(RuntimeProductionHostState& state,
    const std::optional<RuntimeDeathCrateSpawn>& drop) {
    if (!drop) return false;
    const auto& rules = state.rules.getCrateRules();
    std::unordered_set<int> reserved;
    for (const auto& crate : state.skirmishCrates)
        if (crate.active) reserved.insert(runtimeCellKey(crate.cell.x, crate.cell.y));
    if (static_cast<int>(reserved.size()) >= rules.maximum) return false;
    // The source footprint has already been released by the common exit path.
    // Prefer its center, then a nearby reachable tile; never put loot off-map,
    // in another building, on a cliff, or underneath an existing crate.
    const auto candidates = runtimeCratePlacementCandidates(state, reserved);
    const RuntimeCratePlacement* best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (const auto& candidate : candidates) {
        const int dx = candidate.cell.x - drop->center.x;
        const int dy = candidate.cell.y - drop->center.y;
        if (std::abs(dx) > 6 || std::abs(dy) > 6) continue;
        const int distance = dx * dx + dy * dy;
        if (!best || distance < bestDistance || (distance == bestDistance
            && std::pair{candidate.cell.y, candidate.cell.x} < std::pair{best->cell.y, best->cell.x})) {
            best = &candidate; bestDistance = distance;
        }
    }
    if (!best) return false;
    RuntimeCrateState crate;
    crate.cell = best->cell;
    crate.water = best->water;
    crate.authored = true;
    crate.respawnTick = runtimeCrateTickAfter(state, runtimeCrateLifetimeTicks(state, crate, drop->sourceId));
    crate.imageId = runtimeCrateArtImageId(state.rules, crate.water ? rules.waterImage : rules.image);
    if (drop->money) {
        if (const auto* money = runtimeCrateRuleForType(state, "Money"))
            runtimeConfigureCratePowerup(crate, *money);
        else { crate.powerupType = "Money"; crate.powerupAnimation = "MONEY"; crate.powerupData = 2000.0; }
    } else runtimeEnsureCratePowerup(state, crate, drop->sourceId);
    state.skirmishCrates.push_back(std::move(crate));
    state.repaintRequested = true;
    return true;
}

bool advanceRuntimeRepairDepots(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeRepairDepots", state.tick);
    if (!state.repairDepotsInitialized) {
        return false;
    }
    const RuntimeRepairDepotSystemState before = state.repairDepots;
    std::set<std::pair<int, int>> blockedDocks;
    for (std::size_t attempt = 0U;
        attempt <= runtimeAircraftProductionMaximumSlots;
        ++attempt) {
        RuntimeRepairDepotSystemState candidate = before;
        RuntimeRepairDepotContext context =
            makeRuntimeRepairDepotCandidateContext(
                state,
                candidate,
                blockedDocks
            );
        const std::vector<RuntimeRepairDepotAction> actions =
            candidate.tick(context, state.tick);

        const RuntimeRepairDepotAction* padMove = nullptr;
        bool multiplePadMoves = false;
        for (const RuntimeRepairDepotAction& action : actions) {
            if (!runtimeRepairActionClaimsLocalAircraftPad(state, action)) {
                continue;
            }
            if (padMove != nullptr) {
                multiplePadMoves = true;
                break;
            }
            padMove = &action;
        }
        if (multiplePadMoves) {
            state.simulationStatus =
                "repair depot tick exceeded its PadAircraft promotion budget";
            return false;
        }
        if (padMove != nullptr) {
            const RuntimeAircraftProductionMatchResult matched =
                runtimeAircraftProductionMatchForOwner(
                    state,
                    state.economy.owner,
                    std::nullopt,
                    &candidate
                );
            if (!matched.inputValid) {
                state.simulationStatus =
                    "repair depot tick saw invalid PadAircraft authority: "
                    + matched.reason;
                return false;
            }
            if (!matched.feasible) {
                if (!blockedDocks.emplace(
                        padMove->hostId,
                        padMove->dockIndex
                    ).second
                    || attempt == runtimeAircraftProductionMaximumSlots) {
                    state.simulationStatus =
                        "repair depot tick could not find a safe shared dock";
                    return false;
                }
                continue;
            }
        }

        state.repairDepots = std::move(candidate);
        if (!applyRuntimeRepairDepotActions(state, actions)) {
            state.repairDepots = before;
            state.simulationStatus =
                "repair depot tick rejected atomically";
            return false;
        }
        const bool poseChanged = refreshRuntimeRepairDepotDockPoses(state);
        const bool animationChanged = before.animations() != state.repairDepots.animations();
        if (animationChanged) state.repaintRequested = true;
        return !actions.empty() || poseChanged || animationChanged;
    }
    state.simulationStatus = "repair depot tick exhausted shared docks";
    return false;
}

RuntimeRepairDepotArrivalResult completeRuntimeRepairDepotArrival(
    RuntimeProductionHostState& state,
    int unitId) {
    if (!state.repairDepotsInitialized) {
        return RuntimeRepairDepotArrivalResult::NotPending;
    }
    const RuntimeRepairDepotRelation* relation =
        findRuntimeRepairDepotRelationForUnit(state.repairDepots, unitId);
    const WorldObjectInstance* unit = findObjectById(state, unitId);
    if (relation == nullptr
        || relation->phase != RuntimeRepairDepotRelationPhase::Reserved
        || unit == nullptr
        || unit->x != relation->dockCell.x
        || unit->y != relation->dockCell.y
        || unit->onBridge != relation->dockCell.onBridge) {
        return RuntimeRepairDepotArrivalResult::NotPending;
    }

    const int hostId = relation->hostId;
    const RuntimeRepairDepotCell actual{
        unit->x,
        unit->y,
        unit->onBridge
    };
    const RuntimeRepairDepotSystemState before = state.repairDepots;
    RuntimeRepairDepotContext context = makeRuntimeRepairDepotContext(state);
    const RuntimeRepairDepotCommandResult result =
        state.repairDepots.attachAtDock(
            context,
            hostId,
            unitId,
            actual,
            state.tick
        );
    if (!applyRuntimeRepairDepotActions(state, result.actions)) {
        state.repairDepots = before;
        state.simulationStatus =
            "repair depot arrival rejected atomically #"
            + std::to_string(unitId);
        return RuntimeRepairDepotArrivalResult::Released;
    }
    if (result.accepted) {
        state.simulationStatus =
            "repair depot attached #" + std::to_string(unitId);
        return RuntimeRepairDepotArrivalResult::Attached;
    }
    state.simulationStatus =
        "repair depot arrival released #"
        + std::to_string(unitId)
        + " " + result.reason;
    return RuntimeRepairDepotArrivalResult::Released;
}

bool runtimeSelectionCanUseRepairDepot(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject) {
    if (targetObject.kind != WorldObjectKind::Structure
        || targetObject.owner != state.economy.owner) {
        return false;
    }
    const BuildingType* repairBuilding =
        findRuntimeBuildingTypeById(state.rules, targetObject.type);
    if (repairBuilding == nullptr || !repairBuilding->unitRepair) {
        return false;
    }
    for (int objectId : state.selectedObjectIds) {
        const WorldObjectInstance* selected = findObjectById(state, objectId);
        if (selected == nullptr
            || !selected->alive
            || runtimeObjectDisabledByParasite(state, objectId)
            || selected->owner != state.economy.owner
            || selected->kind != WorldObjectKind::Unit) {
            continue;
        }
        const UnitType* unit = findRuntimeUnitType(state.rules, *selected);
        if (unit != nullptr
            && unit->repairable
            && selected->health < runtimeMaxHealthForObject(state, *selected)) {
            return true;
        }
    }
    return false;
}

SuperWeaponTarget runtimeGameplaySuperWeaponTarget(
    const RuntimeMultiplayerGameplaySuperWeaponTarget& target) {
    return {
        {target.cell.x, target.cell.y},
        target.objectId.value_or(-1)
    };
}

bool runtimeGameplaySuperWeaponReadyForCommand(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayFireSuperWeaponCommand& command) {
    if (!state.superWeaponsInitialized) {
        return false;
    }
    SuperWeaponSystem staged = state.superWeapons;
    SuperWeaponWorldContext context = runtimeSuperWeaponContext(state);
    context.emit = {};
    std::string reason;
    if (command.targets.size() == 2U) {
        return staged.submitTwoStageTarget(
            owner,
            command.superWeaponId,
            runtimeGameplaySuperWeaponTarget(command.targets[0]),
            runtimeGameplaySuperWeaponTarget(command.targets[1]),
            context,
            &reason
        );
    }
    if (!staged.beginTargetSelection(
            owner,
            command.superWeaponId,
            context,
            &reason
        )) {
        return false;
    }
    const SuperWeaponTarget target = command.targets.empty()
        ? SuperWeaponTarget{}
        : runtimeGameplaySuperWeaponTarget(command.targets.front());
    return staged.submitTarget(
        owner,
        command.superWeaponId,
        target,
        context,
        &reason
    );
}

const WeaponType* runtimePrismSupportRangeWeapon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& tower) {
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, tower.type);
    if (building == nullptr) {
        return nullptr;
    }
    const std::string weaponId = tower.veterancy >= 2
            && !trimAsciiLocal(building->eliteSecondaryWeapon).empty()
        ? building->eliteSecondaryWeapon
        : building->secondaryWeapon;
    return findRuntimeWeaponType(state.rules, weaponId);
}

bool runtimePrismPrimaryWeaponMatches(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& tower,
    const WeaponType& weapon) {
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, tower.type);
    if (building == nullptr) {
        return false;
    }
    const std::string primaryId = tower.veterancy >= 2
            && !trimAsciiLocal(building->elitePrimaryWeapon).empty()
        ? building->elitePrimaryWeapon
        : building->primaryWeapon;
    return !trimAsciiLocal(primaryId).empty()
        && toLowerAsciiLocal(trimAsciiLocal(primaryId))
            == toLowerAsciiLocal(trimAsciiLocal(weapon.id));
}

void clearRuntimeTemporalAttack(
    RuntimeProductionHostState& state,
    int attackerObjectId,
    const RuntimeObjectBehavior& behavior,
    const std::string& reason) {
    if (!state.specialMechanismsInitialized
        || behavior.kind != RuntimeBehaviorKind::Attack
        || behavior.targetObjectId < 0) {
        return;
    }
    state.specialMechanisms.clearTemporalAttacker(
        attackerObjectId,
        behavior.targetObjectId,
        state.tick,
        runtimeSpecialEventSink(state),
        reason
    );
}

bool advanceRuntimeGarrisonAttackBehavior(
    RuntimeProductionHostState& state,
    WorldObjectInstance& host,
    RuntimeObjectBehavior& behavior,
    bool& handled) {
    handled = !host.garrisonOccupantIds.empty();
    if (!handled) {
        return false;
    }
    if (state.combatDetails.hasPendingBurst(host.instanceId)) return false;
    ensureRuntimeGarrisonsInitialized(state);
    std::optional<WorldObjectInstance> terrainTarget;
    if (behavior.attackGround) {
        terrainTarget = runtimeAttackGroundTargetObject(state, behavior);
    } else if (behavior.attackBridge) {
        terrainTarget = runtimeBridgeAttackTargetObject(state, behavior.attackBridgeCell);
    }
    const bool attackingTerrain = behavior.attackGround || behavior.attackBridge;
    WorldObjectInstance* target = attackingTerrain
        ? (terrainTarget.has_value() ? &*terrainTarget : nullptr)
        : findMutableObjectById(state, behavior.targetObjectId);
    if (target == nullptr
        || !target->alive
        || (!attackingTerrain && !behavior.forceFire
            && runtimeOwnersAreAllied(state, host.owner, target->owner))
        || (!attackingTerrain && !runtimeOwnersAreAllied(state, host.owner, target->owner)
            && !runtimeObjectVisibleToOwner(state, host.owner, *target))
        || (!attackingTerrain && !behavior.forceFire && runtimeSpyDisguiseBlocksAttack(
            state,
            host,
            *target,
            behavior.autoAcquired
        ))) {
        behavior.state = RuntimeBehaviorState::Complete;
        host.mission = runtimeMissionName(RuntimeMissionIntent::Occupying);
        ++state.actionSummary.completedCommands;
        return true;
    }
    if (!runtimeGarrisonHostCanAttackTarget(state, host, *target, behavior.forceFire || attackingTerrain)) {
        behavior.state = behavior.autoAcquired
            ? RuntimeBehaviorState::Complete
            : RuntimeBehaviorState::Blocked;
        host.mission = runtimeMissionName(RuntimeMissionIntent::Occupying);
        if (behavior.autoAcquired) {
            ++state.actionSummary.completedCommands;
        }
        state.simulationStatus = "garrison target unavailable #"
            + std::to_string(host.instanceId)
            + " -> #"
            + std::to_string(target->instanceId);
        return behavior.autoAcquired;
    }
    if (!updateRuntimeWeaponFacingToward(state, host, *target,
            behavior.forceFire || attackingTerrain)) {
        behavior.state = RuntimeBehaviorState::Cooldown;
        host.mission = runtimeMissionName(RuntimeMissionIntent::Occupying);
        return true;
    }

    const std::optional<RuntimeGarrisonFirePlan> plan =
        state.garrisons.takeFirePlan(
            state.rules,
            state.world.accessObjects(),
            host.instanceId,
            target->instanceId,
            state.tick,
            [&state, &host, target](
                int occupantObjectId,
                const WeaponType& candidateWeapon
            ) {
                (void)occupantObjectId;
                return runtimeWeaponHasLineOfSight(
                    state,
                    host,
                    *target,
                    candidateWeapon
                );
            }
        );
    if (!plan.has_value()) {
        behavior.state = RuntimeBehaviorState::Cooldown;
        host.mission = runtimeMissionName(RuntimeMissionIntent::Occupying);
        return false;
    }
    WorldObjectInstance* occupant =
        findMutableRuntimeObjectByIdIncludingInactive(
            state,
            plan->occupantObjectId
        );
    if (!attackingTerrain) target = findMutableObjectById(state, plan->targetObjectId);
    if (occupant == nullptr || target == nullptr || !target->alive) {
        behavior.state = RuntimeBehaviorState::Complete;
        return true;
    }
    RuntimeWeaponProfile profile =
        runtimeGarrisonWeaponProfileForOccupant(state, *occupant, target,
            behavior.forceFire || attackingTerrain);
    profile.occupantMuzzleIndex = plan->occupantIndex < 10 ? plan->occupantIndex : -1;
    const WeaponType* weapon = profile.weapon;
    if (weapon == nullptr
        || toLowerAsciiLocal(weapon->id)
            != toLowerAsciiLocal(plan->weaponId)
        || (weapon->damage <= 0 && !runtimeWeaponHasAttachEffect(state.rules, weapon))) {
        behavior.state = RuntimeBehaviorState::Blocked;
        return false;
    }
    if (weapon->limboLaunch
        || (profile.warhead != nullptr && profile.warhead->parasite)) {
        behavior.state = RuntimeBehaviorState::Blocked;
        state.simulationStatus =
            "parasite surrogate launch rejected";
        return false;
    }
    if (!runtimeWeaponHasLineOfSight(state, host, *target, *weapon)) {
        behavior.state = behavior.autoAcquired
            ? RuntimeBehaviorState::Complete
            : RuntimeBehaviorState::Blocked;
        host.mission = runtimeMissionName(RuntimeMissionIntent::Occupying);
        if (behavior.autoAcquired) {
            ++state.actionSummary.completedCommands;
        }
        state.simulationStatus = "garrison line of sight blocked #"
            + std::to_string(host.instanceId)
            + " -> #"
            + std::to_string(target->instanceId);
        return behavior.autoAcquired;
    }


    const double veteranDamageMultiplier =
        runtimeVeterancyAbilityMultiplier(
            state,
            *occupant,
            VeteranAbility::Firepower
        );
    const int damage = runtimeAuthoredDamageAgainst(
        state,
        *target,
        plan->damage,
        plan->warheadId,
        0.0,
        veteranDamageMultiplier
    );
    (void)damage;
    beginRuntimeCombatBurst(state, host, *target, *weapon, profile,
        veteranDamageMultiplier * (weapon->damage == 0 ? 1.0
            : static_cast<double>(plan->damage) / static_cast<double>(weapon->damage)),
        host.instanceId, behavior.forceFire || attackingTerrain);
    triggerRuntimeWeaponVisuals(state, host);
    host.mission = runtimeMissionName(RuntimeMissionIntent::Occupying);
    if (weapon->fireOnce) {
        behavior.path.clear();
        behavior.pathRequestPending = false;
        behavior.targetObjectId = -1;
        behavior.attackBridge = false;
        behavior.attackGround = false;
        behavior.forceFire = false;
        behavior.weaponCooldownTicks = runtimeWeaponCooldownTicks(
            state,
            *occupant,
            *weapon
        );
        host.weaponCooldownTicksRemaining = behavior.weaponCooldownTicks;
        behavior.state = RuntimeBehaviorState::Cooldown;
        ++state.actionSummary.completedCommands;
    } else {
        behavior.state = RuntimeBehaviorState::Attacking;
    }
    state.simulationStatus = "garrison fire #"
        + std::to_string(host.instanceId)
        + " occupant #"
        + std::to_string(occupant->instanceId)
        + " -> #"
        + std::to_string(target->instanceId)
        + " "
        + plan->weaponId;
    ++state.actionSummary.attacksThisTick;
    return true;
}

bool emitRuntimeMissileExhaust(RuntimeProductionHostState& state, const std::string& animation,
    const std::string& owner, int sourceId, const std::string& missileType,
    const std::array<double, 3>& tail, int elapsedTicks,
    const RuntimeParticleSpriteAttachment& attachment) {
    const auto* art = state.art.findArtType(animation);
    if (art == nullptr || elapsedTicks < 0) return false;
    const int frameDelay = runtimeMissileExhaustFrameDelay(art->rate, state.config.fixedTickHz);
    const int cadence = runtimeMissileExhaustEmissionDelay(art->rate, state.config.fixedTickHz);
    if (elapsedTicks % cadence != 0) return false;
    RuntimeImpactEffect exhaust;
    exhaust.attackerObjectId = sourceId; exhaust.owner = owner;
    exhaust.hasWorldPosition = true;
    exhaust.worldX = tail[0]; exhaust.worldY = tail[1]; exhaust.worldLevel = tail[2];
    exhaust.screenOffsetX = attachment.screenOffsetX;
    exhaust.screenOffsetY = attachment.screenOffsetY;
    exhaust.nativeFixedX = attachment.nativeFixedX;
    exhaust.nativeFixedY = attachment.nativeFixedY;
    exhaust.cell = {static_cast<int>(std::lround(tail[0])), static_cast<int>(std::lround(tail[1]))};
    exhaust.level = static_cast<int>(std::lround(tail[2]));
    exhaust.lifecycleState = "Trailer"; exhaust.renderIntent = "missileExhaust";
    exhaust.projectileId = missileType; exhaust.animId = animation;
    exhaust.trailerIntentId = animation;
    initializeRuntimeImpactAnimationTimeline(state, exhaust, true);
    if (exhaust.animationFrameCount <= 0) return false;
    // Frames 0..2 form the continuously attached nozzle flame in the renderer.
    // Detached puffs continue from frame 3 through the original final frame.
    exhaust.animationStartFrame = std::min(runtimeMissileExhaustFlameFrames,
        exhaust.animationFrameCount - 1);
    exhaust.animationLoopStartFrame = exhaust.animationStartFrame;
    exhaust.animationLoopEndFrame = exhaust.animationEndFrame = exhaust.animationFrameCount;
    exhaust.animationLoopCount = 1;
    exhaust.animationSequenceSteps = exhaust.animationFrameCount - exhaust.animationStartFrame;
    exhaust.frameIndex = exhaust.animationStartFrame;
    exhaust.animationFrameDelayTicks = frameDelay;
    exhaust.totalTicks = exhaust.animationSequenceSteps * frameDelay;
    exhaust.remainingTicks = exhaust.totalTicks;
    state.impactEffects.push_back(std::move(exhaust));
    return true;
}

bool emitRuntimeOrdinaryMissileExhaust(RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& missile, double previousX, double previousY, double previousZ) {
    if (runtimeProjectileIsNuclearDeathDetonation(missile)) return false;
    const auto* type = state.rules.findProjectileType(missile.projectileId);
    const std::string animation = runtimeMissileExhaustImage(type, missile.projectileImageId);
    if (animation.empty() || missile.remainingTicks <= 0) return false;
    const double dx = missile.currentX - previousX, dy = missile.currentY - previousY;
    const double dz = (missile.currentZ - previousZ) / runtimeMissileLevelsPerCell;
    if (std::hypot(std::hypot(dx, dy), dz) < 0.000001) return false;
    const auto* large = state.rules.findSpawnedMissileProfile(missile.projectileId);
    const double bodyLength = large == nullptr ? 32.0 : large->bodyLength;
    const auto origin = animation == "NUKEPUFF"
        ? std::array<double, 3>{missile.currentX, missile.currentY, missile.currentZ}
        : runtimeMissileExhaustOrigin(missile.currentX, missile.currentY,
            missile.currentZ, std::atan2(dy, dx), std::atan2(dz, std::hypot(dx, dy)), bodyLength);
    const bool emitted = emitRuntimeMissileExhaust(state, animation, missile.owner, missile.attackerObjectId,
        missile.projectileId, origin, std::max(0, missile.elapsedTicks - 1));
    if (emitted && animation == "NUKEPUFF") state.impactEffects.back().projectileImageId = missile.projectileImageId;
    return emitted;
}

bool emitRuntimeSuperWeaponMissileExhaust(RuntimeProductionHostState& state) {
    bool changed = false;
    // Nuclear missiles use the super-weapon presentation path rather than
    // the ordinary weapon's projectile collection. Share the original puff.
    for (const auto& missile : state.superWeapons.renderSnapshots()) {
        if (missile.kind != "superWeaponProjectile") continue;
        const auto pose = runtimeSuperWeaponNuclearFlightPose(state, missile);
        if (!pose.visible) continue;
        const auto* type = runtimeNuclearMissileProjectile(state.rules, missile.resourceId);
        if (!type || runtimeMissileExhaustImage(type, type->image) != "NUKEPUFF") continue;
        if (emitRuntimeMissileExhaust(state, "NUKEPUFF", missile.owner, missile.sourceObjectId,
                type->id, {pose.x, pose.y, pose.level}, missile.progress)) {
            state.impactEffects.back().projectileImageId = type->image;
            changed = true;
        }
    }
    return changed;
}

std::string runtimeSpawnedMissileImage(const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    const ArtType* art = runtimeArtTypeForObject(state, object);
    return art == nullptr ? object.type : (art->image.empty() ? art->id : art->image);
}

RuntimeProjectileSourcePosition runtimeMissileDeckPosition(
    const RuntimeProductionHostState& state, const WorldObjectInstance& parent, int slot) {
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

RuntimeInputPixelPoint runtimeMissileDeckOffsetAtCamera(RuntimeProductionHostState& state,
    const WorldObjectInstance& parent, int slot, const RuntimeMapCamera& camera) {
    if (!runtimeSessionGeometryAvailable(state)) return {};
    RuntimeWeaponProfile launcher;
    launcher.slot = RuntimeWeaponSlot::Primary;
    if (const auto* unit = findRuntimeUnitType(state.rules, parent)) {
        const auto* primary = state.rules.findWeaponType(unit->primaryWeapon);
        const auto* secondary = state.rules.findWeaponType(unit->secondaryWeapon);
        if ((!primary || !primary->spawner) && secondary && secondary->spawner)
            launcher.slot = parent.veterancy >= 2 && !unit->eliteSecondaryWeapon.empty()
                ? RuntimeWeaponSlot::EliteSecondary : RuntimeWeaponSlot::Secondary;
        else if (parent.veterancy >= 2 && !unit->elitePrimaryWeapon.empty())
            launcher.slot = RuntimeWeaponSlot::ElitePrimary;
    }
    const auto* art = runtimeArtTypeForObject(state, parent);
    auto flh = runtimeWeaponFireFlh(state, parent, launcher, art);
    if (!flh) return {};
    const auto second = art ? parseRuntimeWeaponFireFlh(art->secondSpawnOffset)
        : std::optional<RuntimeWeaponFireFlh>{};
    if (second && slot > 0) {
        flh->forwardLeptons += second->forwardLeptons;
        flh->lateralLeptons += second->lateralLeptons;
        flh->heightLeptons += second->heightLeptons;
    } else if (slot % 2 != 0) flh->lateralLeptons = -flh->lateralLeptons;
    RuntimeMapRenderContext context;
    context.art = &state.art; context.rules = &state.rules;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.animationTick = state.tick; context.fixedTickHz = state.config.fixedTickHz;
    context.impactActors = runtimeImpactRenderActorsForRender(state);
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    const RuntimeInputPixelRect viewport{0, 0, 1024, 768};
    const auto placement = (state.geometry->runtimeObjectWorldRenderPlacement(parent, state.world, viewport, context, camera, state.renderCache));
    RuntimeInputPixelPoint native{};
    if (!state.geometry->resolveRuntimeVoxelWeaponMuzzlePoint(parent, placement.center, context, camera, state.renderCache,
            flh->forwardLeptons, flh->lateralLeptons, flh->heightLeptons,
            std::max(0, parent.facing), native, false)) return {};
    const auto deck = runtimeMissileDeckPosition(state, parent, slot);
    RuntimeProjectileRenderActor logical;
    logical.hasCurrentPosition = true;
    logical.currentX = deck.x; logical.currentY = deck.y; logical.currentLevel = deck.z;
    const auto point = state.geometry->runtimeProjectileActorScreenPoint(logical, viewport, camera);
    return {native.x - point.x, native.y - point.y};
}

RuntimeMissileDeckPresentation runtimeMissileDeckPresentationOffset(RuntimeProductionHostState& state,
    const WorldObjectInstance& parent, int slot) {
    RuntimeMapCamera camera; camera.initialized = true;
    const auto reference = runtimeMissileDeckOffsetAtCamera(state, parent, slot, camera);
    camera.tileWidth = 120; camera.tileHeight = 60;
    const auto doubled = runtimeMissileDeckOffsetAtCamera(state, parent, slot, camera);
    return {reference.x, reference.y, 2 * reference.x - doubled.x, 2 * reference.y - doubled.y};
}

bool launchRuntimeSpawnedMissileVolley(RuntimeProductionHostState& state,
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

void consumeRuntimeSpawnedMissile(RuntimeProductionHostState& state, const SpawnedMissileState& m,
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

bool advanceRuntimeSpawnedMissiles(RuntimeProductionHostState& state) {
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

void resolveRuntimeSpawnedMissileRenderSnapshots(RuntimeProductionHostState& state) {
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

bool runtimeDiskDrainRecordActive(const RuntimeGameSessionState& state, int sourceId,
    const RuntimeDiskDrain& drain) {
    const auto* source = findObjectById(state, sourceId);
    const auto* target = findObjectById(state, drain.targetId);
    const auto* weapon = state.rules.findWeaponType(drain.weaponId);
    const auto* building = target ? state.rules.findBuildingType(target->type) : nullptr;
    const auto order = state.behaviors.find(sourceId);
    if (!source || !source->alive || source->health <= 0 || !target || !target->alive
        || target->health <= 0 || target->kind != WorldObjectKind::Structure
        || !building || !building->drainable || !weapon || !weapon->drainWeapon
        || source->owner != drain.owner || source->type != drain.sourceType
        || target->owner != drain.targetOwner || target->type != drain.targetType
        || runtimeOwnersAreAllied(state, source->owner, target->owner)
        || order == state.behaviors.end() || order->second.kind != RuntimeBehaviorKind::Attack
        || order->second.targetObjectId != drain.targetId
        || order->second.state == RuntimeBehaviorState::Complete
        || runtimeObjectDisabledByParasite(state, sourceId)
        || runtimeObjectSuspendedByMagBeam(state, sourceId)
        || (state.specialMechanismsInitialized
            && (state.specialMechanisms.isTemporalFrozen(sourceId)
                || state.specialMechanisms.isTemporalFrozen(drain.targetId)))
        || runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(*source), *target) > 0.001)
        return false;
    return runtimeWeaponHasLineOfSight(state, *source, *target, *weapon);
}

bool runtimeDiskDrainOwnerBlackout(const RuntimeGameSessionState& state, const std::string& owner) {
    const auto key = runtimeRepairDepotCanonicalOwnerId(state, owner);
    for (const auto& [id, drain] : state.combatDetails.diskDrains.sources) {
        if (runtimeRepairDepotCanonicalOwnerId(state, drain.targetOwner) != key
            || !runtimeDiskDrainRecordActive(state, id, drain)) continue;
        const auto* target = findObjectById(state, drain.targetId);
        const auto* building = state.rules.findBuildingType(drain.targetType);
        if (target && building && runtimeBuildingPowerOutput(*building, *target) > 0) return true;
    }
    return false;
}

bool runtimeDiskDrainDisablesObject(const RuntimeGameSessionState& state, int objectId) {
    const auto& drains = state.combatDetails.diskDrains;
    const int source = drains.sourceForTarget(objectId);
    const auto found = drains.sources.find(source);
    return found != drains.sources.end() && runtimeDiskDrainRecordActive(state, source, found->second);
}

void cancelRuntimeDiskDrainSource(RuntimeGameSessionState& state, int sourceId) {
    if (state.combatDetails.diskDrains.sources.erase(sourceId)) state.powerRefreshPending = true;
}

bool beginRuntimeDiskDrain(RuntimeProductionHostState& state, WorldObjectInstance& source,
    const WorldObjectInstance& target, const WeaponType& weapon, RuntimeObjectBehavior& behavior) {
    auto& drains = state.combatDetails.diskDrains;
    const auto* building = state.rules.findBuildingType(target.type);
    if (!weapon.drainWeapon || target.kind != WorldObjectKind::Structure || !building
        || !building->drainable || runtimeOwnersAreAllied(state, source.owner, target.owner)
        || runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(source), target) > 0.001)
        return false;
    const int competingSource = drains.sourceForTarget(target.instanceId);
    if (competingSource >= 0 && competingSource != source.instanceId) {
        const auto& other = drains.sources.at(competingSource);
        if (runtimeDiskDrainRecordActive(state, competingSource, other)) {
            behavior.state = RuntimeBehaviorState::Cooldown;
            return false;
        }
        cancelRuntimeDiskDrainSource(state, competingSource);
    }
    auto existing = drains.sources.find(source.instanceId);
    if (existing == drains.sources.end() || existing->second.targetId != target.instanceId
        || existing->second.targetOwner != target.owner || existing->second.owner != source.owner) {
        RuntimeDiskDrain drain;
        drain.targetId = target.instanceId;
        drain.startedTick = drain.lastAdvancedTick = state.tick;
        // Retail 0x6FA167 checks CurrentFrame % DrainMoneyFrameDelay.
        drain.nextMoneyTick = runtimeDiskDrainNextMoneyTick(state.tick,
            state.rules.getSpecialMechanismRules().drainMoneyFrameDelay, state.config.fixedTickHz);
        drain.owner = source.owner; drain.sourceType = source.type;
        drain.targetOwner = target.owner; drain.targetType = target.type; drain.weaponId = weapon.id;
        drains.sources.insert_or_assign(source.instanceId, std::move(drain));
        state.powerRefreshPending = true;
        if (weapon.decloakToFire) runtimeDecloakObject(state, source, "drain");
        notifyRuntimeWeaponFired(state, source, target, weapon);
        ++state.actionSummary.attacksThisTick;
    }
    // DrainWeapon establishes a link, not a damaging projectile or a paid burst.
    // Its authored FireOnce must not clear that persistent attack target.
    behavior.path.clear();
    behavior.pathRequestPending = false;
    behavior.state = RuntimeBehaviorState::Cooldown;
    source.mission = "Attacking";
    return true;
}

bool advanceRuntimeDiskDrains(RuntimeProductionHostState& state, bool transferMoney) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeDiskDrains", state.tick);
    auto& drains = state.combatDetails.diskDrains.sources;
    bool changed = false;
    const auto& rules = state.rules.getSpecialMechanismRules();
    for (auto it = drains.begin(); it != drains.end();) {
        if (!runtimeDiskDrainRecordActive(state, it->first, it->second)) {
            it = drains.erase(it);
            state.powerRefreshPending = true;
            changed = true;
            continue;
        }
        auto& drain = it->second;
        if (transferMoney && state.tick > drain.lastAdvancedTick) {
            drain.lastAdvancedTick = state.tick;
            const auto* building = state.rules.findBuildingType(drain.targetType);
            if (state.tick >= drain.nextMoneyTick) {
                if (building && building->resourceDestination) {
                    const int available = std::max(0, runtimeRepairDepotCreditBalance(state, drain.targetOwner));
                    const int receiver = std::max(0, runtimeRepairDepotCreditBalance(state, drain.owner));
                    const int amount = std::min({rules.drainMoneyAmount, available,
                        std::numeric_limits<int>::max() - receiver});
                    if (amount > 0) {
                        debitRuntimeRepairDepotCredits(state, drain.targetOwner, amount);
                        depositRuntimeRepairDepotCredits(state, drain.owner, amount);
                    }
                }
                drain.nextMoneyTick = runtimeDiskDrainNextMoneyTick(
                    state.tick, rules.drainMoneyFrameDelay, state.config.fixedTickHz);
            }
            changed = true;
        }
        ++it;
    }
    return changed;
}

bool advanceRuntimeAttackBehavior(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior) {
    RuntimePerfTraceScope trace("advanceRuntimeAttackBehavior", state.tick);
    const auto deckFlight = state.specialMechanisms.carrierChildren().find(object.instanceId);
    if (runtimeCarrierHasDeckFlight(state, object)
        && deckFlight != state.specialMechanisms.carrierChildren().end()
        && deckFlight->second.phase == CarrierChildPhase::Launching) return false;
    const auto clearAttackMovePose = [&]() {
        const auto sortie = state.specialMechanisms.carrierChildren().find(object.instanceId);
        if (object.airborne && sortie != state.specialMechanisms.carrierChildren().end()
            && sortie->second.phase != CarrierChildPhase::Docked
            && sortie->second.phase != CarrierChildPhase::Destroyed
            && !state.rules.findSpawnedMissileProfile(object.type)) return;
        if (!worldObjectHasStoppedGroundPose(object)) clearRuntimeRenderPosition(object);
    };
    // An accepted order waits for construction to finish; it must not turn
    // the hidden turret or fire while the SHP still owns the whole building.
    if (object.kind == WorldObjectKind::Structure && object.buildAnimTicksRemaining > 0) return false;
    for (const auto& strike : state.airstrikes.strikes()) {
        if (!strike.cancelled && strike.callerObjectId == object.instanceId
            && strike.targetObjectId == behavior.targetObjectId) {
            behavior.state = RuntimeBehaviorState::Cooldown;
            clearAttackMovePose();
            return false; // The accepted designator owns this attack until the flight ends.
        }
    }
    if (holdRuntimeChronoRecovery(state, object, behavior)) return false;
    // Guard retaliation may fire while stopped. Release a frozen flight
    // only when this attack actually needs to chase a target below.
    resumeRuntimeJumpjetSegment(state, object, behavior, false);
    if (state.combatDetails.hasPendingBurst(object.instanceId)) return false;
    // A negative ordinary target id marks a fire-once attack whose target was
    // cleared. Terrain targets also use negative synthetic ids, but remain live
    // through attackBridgeCell while the attacker turns and cools down.
    if (!behavior.attackBridge && !behavior.attackGround
        && behavior.targetObjectId < 0
        && behavior.state == RuntimeBehaviorState::Cooldown) {
        if (object.weaponCooldownTicksRemaining > 0) {
            behavior.weaponCooldownTicks =
                object.weaponCooldownTicksRemaining;
        } else if (behavior.weaponCooldownTicks > 0) {
            --behavior.weaponCooldownTicks;
        }
        if (behavior.weaponCooldownTicks <= 0
            && object.weaponCooldownTicksRemaining <= 0) {
            behavior.weaponCooldownTicks = 0;
            behavior.state = RuntimeBehaviorState::Complete;
        }
        return true;
    }
    if (runtimeObjectDisabledByParasite(state, object.instanceId)) {
        behavior.state = RuntimeBehaviorState::Cooldown;
        object.mission = state.specialMechanisms.isParasiteHostParalyzed(
            object.instanceId
        ) ? "ParasiteParalyzed" : "ParasiteStunned";
        return false;
    }
    if (!runtimeObjectHasOperationalPower(state, object)) {
        clearRuntimeTemporalAttack(
            state,
            object.instanceId,
            behavior,
            "attacker power interrupted"
        );
        behavior.state = RuntimeBehaviorState::Cooldown;
        setRuntimeMissionIntent(
            state,
            object,
            object.garrisonOccupantIds.empty()
                ? RuntimeMissionIntent::Idle
                : RuntimeMissionIntent::Occupying,
            "lowPower"
        );
        state.simulationStatus =
            "low power paused #" + std::to_string(object.instanceId);
        return false;
    }
    if (state.specialMechanismsInitialized
        && state.specialMechanisms.isTemporalFrozen(object.instanceId)) {
        clearRuntimeTemporalAttack(state, object.instanceId, behavior,
            "temporal attacker frozen");
        behavior.state = RuntimeBehaviorState::Cooldown;
        object.mission = "Temporal";
        return false;
    }
    // The overrun can remove the attack target halfway through this segment.
    // Finish the accepted physical movement before target-loss cleanup; that
    // cleanup must not snap the body to either navigation cell.
    if (runtimeObjectUsesOmniCrushApproach(state, object)
        && object.hasRenderPosition && object.renderMoveActive
        && behavior.renderMoveTicksTotal > 0 && behavior.moveCooldownTicks > 0) {
        if (cancelRuntimeBlockedOmniCrushApproach(state, object, behavior)) return true;
        if (runtimeGroundRenderStepWaitsForStoppedBody(state, object, behavior)) return false;
        behavior.state = RuntimeBehaviorState::Cooldown;
        const bool visualChanged = advanceRuntimeRenderMove(state, object, behavior);
        --behavior.moveCooldownTicks;
        if (behavior.moveCooldownTicks <= 0
            && !runtimeOmniCrushPendingSegment(state, object, behavior))
            behavior.moveSegmentFrom = {-1, -1};
        return visualChanged;
    }
    bool garrisonHandled = false;
    const bool garrisonChanged = advanceRuntimeGarrisonAttackBehavior(
        state,
        object,
        behavior,
        garrisonHandled
    );
    if (garrisonHandled) {
        return garrisonChanged;
    }
    const UnitType* ammoRules = runtimeAmmoRulesForObject(
        state.rules,
        object
    );
    if (ammoRules != nullptr
        && ammoRules->ammo >= 0
        && object.currentAmmo <= 0) {
        clearRuntimeTemporalAttack(
            state,
            object.instanceId,
            behavior,
            "attack stopped: magazine empty"
        );
        behavior.path.clear();
        behavior.pathRequestPending = false;
        behavior.state = RuntimeBehaviorState::Complete;
        object.mission = behavior.returnToDeployed
            ? runtimeMissionName(RuntimeMissionIntent::Deployed)
            : (behavior.autoAcquired
                ? "Guard"
                : runtimeMissionName(RuntimeMissionIntent::Idle));
        clearAttackMovePose();
        ++state.actionSummary.completedCommands;
        state.simulationStatus =
            "empty magazine #" + std::to_string(object.instanceId);
        return true;
    }
    if (ammoRules != nullptr
        && ammoRules->airportBound
        && !object.airborne) {
        departRuntimeAircraftDock(state, object);
    }
    std::optional<WorldObjectInstance> bridgeTarget;
    WorldObjectInstance* target = nullptr;
    if (behavior.attackGround) {
        bridgeTarget = runtimeAttackGroundTargetObject(state, behavior);
        if (bridgeTarget.has_value()) {
            target = &*bridgeTarget;
        }
    } else if (behavior.attackBridge) {
        bridgeTarget = runtimeBridgeAttackTargetObject(
            state,
            behavior.attackBridgeCell
        );
        if (bridgeTarget.has_value()) {
            target = &*bridgeTarget;
        }
    } else {
        target = findMutableObjectById(state, behavior.targetObjectId);
    }
    if (target != nullptr
        && target->alive
        && !behavior.attackGround && !behavior.attackBridge
        && !runtimeOwnersAreAllied(state, object.owner, target->owner)
        && (!runtimeObjectVisibleToOwner(state, object.owner, *target)
            || (!behavior.forceFire && runtimeSpyDisguiseBlocksAttack(
                state,
                object,
                *target,
                behavior.autoAcquired
            )))) {
        target = nullptr;
    }
    if (target == nullptr || !target->alive) {
        clearRuntimeTemporalAttack(
            state,
            object.instanceId,
            behavior,
            "attack target lost"
        );
        object.mission =
            behavior.returnToDeployed
                ? runtimeMissionName(RuntimeMissionIntent::Deployed)
                : (behavior.autoAcquired
                    ? "Guard"
                    : runtimeMissionName(RuntimeMissionIntent::Idle));
        clearAttackMovePose();
        if (!behavior.autoAcquired
            && !updateRuntimeWeaponFacingTowardBody(state, object)) {
            behavior.state = RuntimeBehaviorState::Cooldown;
            state.simulationStatus =
                "turret returning #"
                + std::to_string(object.instanceId);
            return true;
        }

        behavior.state = RuntimeBehaviorState::Complete;
        ++state.actionSummary.completedCommands;
        return true;
    }

    if (!behavior.attackGround && !behavior.attackBridge && !behavior.forceFire
        && object.weaponCooldownTicksRemaining <= 0) {
        const UnitType* firingUnit = findRuntimeUnitType(state.rules, object);
        if (firingUnit != nullptr && firingUnit->distributedFire) {
            if (const auto* distributedTarget = selectRuntimeCombatOpportunityTarget(
                    state, object, nullptr, 0.0, true)) {
                target = findMutableObjectById(state, distributedTarget->instanceId);
                if (behavior.targetObjectId != distributedTarget->instanceId)
                    state.gattlingWeaponTimers.erase(object.instanceId);
                behavior.targetObjectId = distributedTarget->instanceId;
            }
        }
    }
    const RuntimeWeaponProfile weaponProfile =
        runtimeWeaponProfileForObject(state, object, target, behavior.forceFire);
    const WeaponType* weapon = weaponProfile.weapon;
    if ((behavior.attackBridge && runtimeWeaponUsesBridgeHutDemolition(weapon, weaponProfile.warhead))
        || !runtimeBridgeHutDemolitionCommandLegal(state, *target, weapon, weaponProfile.warhead,
            runtimeObjectHasC4Capability(state.rules, object))) {
        behavior.state = RuntimeBehaviorState::Complete;
        behavior.path.clear();
        behavior.pathRequestPending = false;
        object.mission = behavior.returnToDeployed ? runtimeMissionName(RuntimeMissionIntent::Deployed)
            : runtimeMissionName(RuntimeMissionIntent::Idle);
        clearAttackMovePose();
        state.simulationStatus = "bridge demolition needs a valid repair hut";
        return true;
    }
    int& firingCooldownTicks = weapon != nullptr && weapon->areaFire && weapon->radLevel > 0
        && behavior.returnToDeployed ? object.radiationEruptionCooldownTicksRemaining
        : object.weaponCooldownTicksRemaining;
    const bool areaFireSelf = weapon != nullptr
        && weapon->areaFire
        && target->instanceId == object.instanceId;
    const bool alliedTarget =
        weapon != nullptr
        && runtimeOwnersAreAllied(state, object.owner, target->owner);
    const bool repairTarget =
        weapon != nullptr && weapon->damage < 0;
    const bool repairComplete =
        repairTarget
        && target->health >= runtimeMaxHealthForObject(state, *target) && !(runtimeActiveShield(*target, state.rules, weaponProfile.warhead) && target->modExtensions.shieldHealth < target->modExtensions.shieldCapacity);
    const bool timedBombRelationship = weaponProfile.warhead != nullptr
        && ((weaponProfile.warhead->bombDisarm
                && state.specialMechanismsInitialized
                && state.specialMechanisms.timedBombs().find(
                    target->instanceId
                ) != state.specialMechanisms.timedBombs().end())
            || weaponProfile.warhead->ivanBomb);
    const bool disguiseRelationship = weaponProfile.warhead != nullptr
        && weaponProfile.warhead->makesDisguise;
    const bool disguiseRelationshipLegal = disguiseRelationship
        && runtimeSpyCanApplyMakesDisguise(
            state,
            object.instanceId,
            target->instanceId,
            weaponProfile.warhead->id
        );
    const bool infiniteDisguiseRange = disguiseRelationshipLegal
        && weapon != nullptr && weapon->range < 0.0;
    const bool verticalBombAttack = object.airborne && weaponProfile.projectile != nullptr
        && weaponProfile.projectile->vertical && !weaponProfile.projectile->inviso
        && !weaponProfile.projectile->imageIsNone
        && !trimAsciiLocal(weaponProfile.projectile->image).empty();
    const bool kirovBuildingBombAttack = !behavior.attackGround && !behavior.attackBridge
        && runtimeKirovBuildingBombProfile(object, *target, weaponProfile);
    const RuntimeProjectileTargetPosition kirovBuildingAim = kirovBuildingBombAttack
        ? runtimeProjectileTargetPosition(state, *target) : RuntimeProjectileTargetPosition{};
    const bool omniCrushPursuit = runtimeObjectUsesOmniCrushApproach(state, object)
        && target->kind != WorldObjectKind::Structure
        && runtimeCrusherCanCrushObject(state, object, *target, runtimeCellPointForObject(*target));
    // Retail starts DrainWeapon only when the source cell contains its
    // target building (TechnoClass 0x70FD70), so approach the foundation.
    const double effectiveAttackRange = omniCrushPursuit || (weapon && weapon->drainWeapon) ? 0.0
        : verticalBombAttack && toUpperAsciiLocal(object.type) != "ZEP" ? 0.25 : infiniteDisguiseRange
        ? std::numeric_limits<double>::infinity()
        : (weapon == nullptr
            ? 0.0
            : (areaFireSelf
                ? std::numeric_limits<double>::infinity()
                : runtimeEffectiveWeaponRange(
                    state,
                    object,
                    *target,
                    *weapon
                )));
    const bool ordinaryAttackRelationLegal = behavior.autoAcquired
        ? (areaFireSelf || runtimeOwnersAreHostileForAutoTarget(
            state,
            object.owner,
            target->owner
        ))
        : (areaFireSelf || runtimeOrdinaryAttackRelationLegal(
            state, object, *target, behavior.forceFire));
    const bool psychicAttackLegal = repairTarget
        || timedBombRelationship || disguiseRelationshipLegal
        || runtimePsychicCanAttackTarget(
            state,
            object,
            *target,
            true,
            ordinaryAttackRelationLegal
        );
    if (weapon == nullptr
        || (weaponProfile.warhead != nullptr && weaponProfile.warhead->ivanBomb
            && !runtimeBuildingBombOrderRelationLegal(state, object, *target, behavior.forceFire))
        || (weapon->damage == 0 && !runtimeWeaponHasAttachEffect(state.rules, weapon) && !timedBombRelationship
            && !disguiseRelationshipLegal)
        || (!runtimeWeaponHasAttackRange(*weapon) && !infiniteDisguiseRange && !areaFireSelf)
        || (disguiseRelationship && !disguiseRelationshipLegal)
        || (repairTarget && (!alliedTarget || repairComplete))
        || (!repairTarget && !timedBombRelationship
            && !disguiseRelationshipLegal
            && !psychicAttackLegal)) {
        clearRuntimeTemporalAttack(
            state,
            object.instanceId,
            behavior,
            "attack target invalid"
        );
        object.mission =
            behavior.returnToDeployed
                ? runtimeMissionName(RuntimeMissionIntent::Deployed)
                : (behavior.autoAcquired
                    ? "Guard"
                    : runtimeMissionName(RuntimeMissionIntent::Idle));
        clearAttackMovePose();
        if (!behavior.autoAcquired
            && !updateRuntimeWeaponFacingTowardBody(state, object)) {
            behavior.state = RuntimeBehaviorState::Cooldown;
            state.simulationStatus =
                "turret returning #"
                + std::to_string(object.instanceId);
            return true;
        }

        behavior.state = RuntimeBehaviorState::Complete;
        ++state.actionSummary.completedCommands;
        return true;
    }

    if (const auto* tractor = runtimeMagBeamBindingForSource(state, object.instanceId);
        tractor != nullptr && tractor->targetObjectId == target->instanceId
        && tractor->phase != RuntimeMagBeamPhase::Falling
        && runtimeMagBeamSourceMaintainsLock(state, *tractor)) {
        // MinimumRange gates starting a shot, not maintaining a live tractor.
        // Let the locomotor finish delivery without chasing its own victim.
        behavior.state = RuntimeBehaviorState::Attacking;
        clearAttackMovePose();
        return false;
    }

    if (cancelRuntimeBlockedOmniCrushApproach(state, object, behavior)) return true;
    bool kirovMovementChanged = false;
    if (behavior.moveCooldownTicks > 0) {
        if (runtimeGroundRenderStepWaitsForStoppedBody(state, object, behavior)) {
            behavior.state = RuntimeBehaviorState::Blocked;
            return false;
        }
        behavior.state = RuntimeBehaviorState::Cooldown;
        const bool visualChanged = object.airborne && runtimeJumpjetProfile(state, object)
            ? advanceRuntimeJumpjetSegment(state, object, behavior) : advanceRuntimeRenderMove(state, object, behavior);
        --behavior.moveCooldownTicks;
        if (behavior.moveCooldownTicks <= 0) {
            if (runtimeOmniCrushPendingSegment(state, object, behavior)) return visualChanged;
            behavior.moveSegmentFrom = {-1, -1};
            if (!kirovBuildingBombAttack) {
                clearAttackMovePose();
                // Legacy carrier saves can finish their already committed cell segment.
                // Begin continuous travel next tick, preserving body and Speed at the boundary.
                if (runtimeCarrierHasContinuousAttackFlight(state, object)) return visualChanged;
                return advanceRuntimeAttackBehavior(state, object, behavior)
                    || visualChanged;
            }
        }
        if (!kirovBuildingBombAttack) return visualChanged;
        // The global Jumpjet physics and this segment share its tick guard.
        // Continue to the original firing tail without recursion or a second shot.
        kirovMovementChanged = visualChanged;
    }

    if (object.airborne && runtimeFixedWingProfile(state,object)) {
        auto& flight=runtimeAircraftFlightFor(state,object);flight.moving=false;
        applyRuntimeFixedWingPose(state,object);
    }
    if (kirovBuildingBombAttack) {
        const auto flight = state.jumpjetFlights.find(object.instanceId);
        const bool moving = flight != state.jumpjetFlights.end() && flight->second.active;
        const bool stopped = flight != state.jumpjetFlights.end() && flight->second.stopped;
        const double currentDistance = std::hypot(
            runtimeObjectRenderX(object) - kirovBuildingAim.x,
            runtimeObjectRenderY(object) - kirovBuildingAim.y);
        // Retain stopped Guard retaliation in range. An explicit attack, or a
        // target leaving reach/LOS, may resume the already frozen segment.
        const bool mayResume = !stopped || !behavior.autoAcquired
            || currentDistance > effectiveAttackRange
            || !runtimeWeaponHasLineOfSight(state, object, *target, *weapon);
        if (!moving && mayResume
            && (stopped || runtimeCellPointForObject(object) != kirovBuildingAim.cell))
            kirovMovementChanged = advanceRuntimeKirovBuildingBombApproach(
                state, object, behavior, *target) || kirovMovementChanged;
    }
    // Cell commitment can lead the visible body by a whole segment. Check the
    // actual emission body's horizontal distance to the original true center
    // after any carried movement, retaining the authored 1.5 firing radius.
    const double distance = kirovBuildingBombAttack
        ? std::hypot(runtimeObjectRenderX(object) - kirovBuildingAim.x,
            runtimeObjectRenderY(object) - kirovBuildingAim.y)
        : weapon->drainWeapon
        ? runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(object), *target)
        : runtimeWeaponDistanceToTarget(state, object, *target, *weapon);
    const bool insideMinimumRange = !areaFireSelf && distance + 0.001 < weapon->minimumRange;
    static const bool unreachableLosReference = [] {
        const char* flag = std::getenv("CONCORD_COMBAT_RANGE_LOS_REFERENCE");
        return flag != nullptr && std::strcmp(flag, "1") == 0;
    }();
    const bool hasLineOfSight = areaFireSelf
        || (!unreachableLosReference && (insideMinimumRange || distance > effectiveAttackRange))
        || runtimeWeaponHasLineOfSight(state, object, *target, *weapon);
    const std::string normalizedAttackMission =
        toLowerAsciiLocal(trimAsciiLocal(object.mission));
    const bool delayedFirePending =
        normalizedAttackMission == "delayedfire"
        || normalizedAttackMission == "deployeddelayedfire";
    if (insideMinimumRange
        && (behavior.autoAcquired || behavior.returnToDeployed || !runtimeObjectCanMove(state, object))) {
        if (delayedFirePending) {
            behavior.weaponCooldownTicks = 0;
        }
        if (behavior.autoAcquired) {
            object.mission = behavior.returnToDeployed
                ? runtimeMissionName(RuntimeMissionIntent::Deployed)
                : "Guard";
            behavior.path.clear();
            behavior.state = RuntimeBehaviorState::Complete;
            clearRuntimeTemporalAttack(
                state,
                object.instanceId,
                behavior,
                "temporal attack completed without target"
            );
            ++state.actionSummary.completedCommands;
            state.simulationStatus =
                "auto target inside minimum range #"
                + std::to_string(object.instanceId);
            return true;
        }
        clearRuntimeTemporalAttack(
            state,
            object.instanceId,
            behavior,
            "temporal attack inside minimum range"
        );
        behavior.state = RuntimeBehaviorState::Blocked;
        state.simulationStatus =
            "target inside minimum range #"
            + std::to_string(object.instanceId);
        return false;
    }
    if (kirovBuildingBombAttack
        && (insideMinimumRange || distance > effectiveAttackRange || !hasLineOfSight))
        return kirovMovementChanged;
    if (!kirovBuildingBombAttack
        && (insideMinimumRange || distance > effectiveAttackRange || !hasLineOfSight)) {
        // A moving target can invalidate an already planned retreat. Reuse the
        // normal attack path queue so Stop, save/load and command ownership stay shared.
        if (insideMinimumRange && !behavior.path.empty()
            && behavior.pendingPathGoal != runtimeCellPointForObject(*target)) {
            behavior.path.clear();
        }
        clearRuntimeTemporalAttack(state, object.instanceId, behavior,
            "temporal target left weapon reach");
        if (delayedFirePending) {
            behavior.weaponCooldownTicks = 0;
        }
        if (!runtimeObjectCanMove(state, object)) {
            if (behavior.autoAcquired) {
                object.mission = behavior.returnToDeployed
                    ? runtimeMissionName(RuntimeMissionIntent::Deployed)
                    : "Guard";
                behavior.path.clear();
                behavior.state = RuntimeBehaviorState::Complete;
                ++state.actionSummary.completedCommands;
                state.simulationStatus =
                    "auto target left defense range #"
                    + std::to_string(object.instanceId);
                return true;
            }
            clearRuntimeTemporalAttack(
                state,
                object.instanceId,
                behavior,
                "temporal attacker cannot leave defense range"
            );
            behavior.state = RuntimeBehaviorState::Blocked;
            state.simulationStatus =
                "target out of range #"
                + std::to_string(object.instanceId);
            return false;
        }

        const auto frozenFlight = state.jumpjetFlights.find(object.instanceId);
        if (frozenFlight != state.jumpjetFlights.end() && frozenFlight->second.stopped) {
            resumeRuntimeJumpjetSegment(state, object, behavior);
            return advanceRuntimeAttackBehavior(state, object, behavior);
        }
        const bool carrierFlight = runtimeCarrierHasContinuousAttackFlight(state, object);
        if (object.airborne && (runtimeFixedWingProfile(state,object) || carrierFlight)) {
            double goalX=target->hasRenderPosition?target->renderX:target->x;
            double goalY=target->hasRenderPosition?target->renderY:target->y;
            if (insideMinimumRange) {
                const double x=object.renderX,y=object.renderY;
                double dx=x-goalX,dy=y-goalY;const double length=std::hypot(dx,dy);
                if(length<1e-8){dx=1;dy=0;}else{dx/=length;dy/=length;}
                goalX=x+dx*(weapon->minimumRange+1);goalY=y+dy*(weapon->minimumRange+1);
            }
            return carrierFlight
                ? advanceRuntimeCarrierAircraftToward(state,object,behavior,goalX,goalY)
                : advanceRuntimeFixedWingToward(state,object,behavior,goalX,goalY);
        }

        if (behavior.pathRequestPending) {
            behavior.state = RuntimeBehaviorState::Chasing;
            if (!behavior.returnToDeployed) {
                setRuntimeMissionIntent(
                    state,
                    object,
                    RuntimeMissionIntent::Idle,
                    "attackPathPending"
                );
            } else {
                setRuntimeMissionIntent(
                    state,
                    object,
                    RuntimeMissionIntent::Deployed,
                    "attackPathPendingDeployed"
                );
            }
            return false;
        }

        if (behavior.path.empty()) {
            if (state.tick < behavior.nextRepathTick) {
                behavior.state = RuntimeBehaviorState::Blocked;
                state.simulationStatus =
                    "attack path cooldown #"
                    + std::to_string(object.instanceId);
                return false;
            }
            submitRuntimeAttackPathRequest(
                state,
                object,
                behavior,
                *target,
                effectiveAttackRange,
                0,
                behavior.pendingIgnoredObjectIds.empty()
                    ? nullptr
                    : &behavior.pendingIgnoredObjectIds
            );
            if (!behavior.returnToDeployed) {
                setRuntimeMissionIntent(
                    state,
                    object,
                    RuntimeMissionIntent::Idle,
                    "attackPathRequested"
                );
            } else {
                setRuntimeMissionIntent(
                    state,
                    object,
                    RuntimeMissionIntent::Deployed,
                    "attackPathRequestedDeployed"
                );
            }
            return false;
        }

        if (const auto chrono = advanceRuntimeTeleporterMove(state, object, behavior, behavior.path.back())) {
            return *chrono;
        }
        const RuntimeCellPoint next = behavior.path.front();
        if (runtimeInfantryUsesGroundSubcells(object)
            || behavior.factoryExitClearanceHostId >= 0
            || (!state.factoryDeliveries.leases().empty()
                && runtimeFactoryLandDeliveryOwnsEgress(state, object)))
            (void)runtimeTickOccupancyLookup(state);
        const auto infantryAdmission = runtimeGroundBodyTransitionAdmission(
            state, object, runtimeCellPointForObject(object), next);
        const bool infantryBodyBlocked = infantryAdmission.bodyBlocked;
        if (infantryAdmission.held) {
            behavior.state=RuntimeBehaviorState::Blocked;
            return false;
        }
        std::unordered_map<int, const TerrainCell*> generatedTerrainByCell;
        const std::unordered_map<int, const TerrainCell*>* terrainByCell =
            &state.terrainByCell;
        if (terrainByCell->empty()) {
            generatedTerrainByCell = buildRuntimeTerrainLookup(state.world);
            terrainByCell = &generatedTerrainByCell;
        }
        const std::unordered_map<int, int>& occupiedByCell =
            runtimeTickOccupancyLookup(state);
        const RuntimeCellPoint current = runtimeCellPointForObject(object);
        if (infantryBodyBlocked || !runtimeCellTransitionIsPassable(
                state,
                *terrainByCell,
                occupiedByCell,
                current,
                next,
                object
            )) {
            int blockerObjectId = -1;
            if (!infantryBodyBlocked && runtimeShouldWaitForMovingBlocker(
                    state,
                    occupiedByCell,
                    next,
                    object,
                    behavior.pendingIgnoredObjectIds.empty()
                        ? nullptr
                        : &behavior.pendingIgnoredObjectIds,
                    behavior.blockedTicks,
                    blockerObjectId
                )) {
                ++behavior.blockedTicks;
                behavior.state = RuntimeBehaviorState::Blocked;
                state.simulationStatus =
                    "waiting for blocker #"
                    + std::to_string(blockerObjectId)
                    + " while chasing #"
                    + std::to_string(target->instanceId);
                return false;
            }

            ++behavior.blockedTicks;
            if (infantryBodyBlocked
                && tryRuntimeLocalAvoidanceMove(state, object, behavior, true, "attackBodyBlocked")) {
                return true;
            }
            behavior.path.clear();
            if (infantryBodyBlocked)
                releaseRuntimeInfantrySubcellReservation(state, object.instanceId);
            behavior.state = RuntimeBehaviorState::Blocked;
            submitRuntimeAttackPathRequest(
                state,
                object,
                behavior,
                *target,
                effectiveAttackRange,
                runtimeRepathCooldownTicks,
                behavior.pendingIgnoredObjectIds.empty()
                    ? nullptr
                    : &behavior.pendingIgnoredObjectIds
            );
            return false;
        }

        const RuntimeCellPoint previous = current;
        if (runtimeInfantryUsesGroundSubcells(object)
            && !runtimeInfantryReservationMatches(
                state,
                object.instanceId,
                next
            )) {
            behavior.state = RuntimeBehaviorState::Blocked;
            state.simulationStatus =
                "attack subcell reservation pending #"
                + std::to_string(object.instanceId);
            return false;
        }
        if (!updateRuntimeMoveFacingToward(state, object, previous, next)) {
            behavior.state = RuntimeBehaviorState::Cooldown;
            if (!behavior.returnToDeployed) {
                setRuntimeMissionIntent(
                    state,
                    object,
                    RuntimeMissionIntent::Idle,
                    "attackTurnToMove"
                );
            } else {
                setRuntimeMissionIntent(
                    state,
                    object,
                    RuntimeMissionIntent::Deployed,
                    "attackTurnToMoveDeployed"
                );
            }
            state.simulationStatus =
                "turning #"
                + std::to_string(object.instanceId)
                + " toward #"
                + std::to_string(target->instanceId);
            return true;
        }

        const double sourceLevel = static_cast<double>(object.level);
        const bool sourceOnBridge = object.onBridge;
        const RuntimeCrushStepResult crushResult =
            resolveRuntimeCrushAtDestination(
                state,
                object.instanceId,
                next
            );
        if (crushResult == RuntimeCrushStepResult::Blocked) {
            ++behavior.blockedTicks;
            behavior.state = RuntimeBehaviorState::Blocked;
            state.simulationStatus =
                "attack chase crush blocked #"
                + std::to_string(object.instanceId);
            return false;
        }
        if (!object.alive) {
            return true;
        }
        behavior.path.pop_front();
        if (runtimeInfantryUsesGroundSubcells(object)) {
            if (!commitRuntimeInfantryMoveDestination(
                    state,
                    object,
                    next
                )) {
                releaseRuntimeInfantrySubcellReservation(
                    state,
                    object.instanceId
                );
                behavior.path.clear();
                behavior.state = RuntimeBehaviorState::Blocked;
                behavior.nextRepathTick = state.tick + 1;
                state.simulationStatus =
                    "attack subcell commit blocked #"
                    + std::to_string(object.instanceId);
                return false;
            }
        } else if (crushResult != RuntimeCrushStepResult::Approaching) {
            setRuntimeObjectCell(state, object, next, false);
        }
        if (crushResult != RuntimeCrushStepResult::Approaching)
            updateRuntimeTickOccupancyForMove(state, object, previous, next);
        behavior.moveCooldownTicks = consumeRuntimeMovementSegmentIntervalTicks(
            state,
            object,
            behavior,
            previous,
            next
        );
        beginRuntimeRenderMove(
            object,
            behavior,
            previous,
            next,
            sourceLevel,
            sourceOnBridge,
            behavior.moveCooldownTicks,
            &state
        );
        behavior.state = RuntimeBehaviorState::Chasing;
        if (!behavior.returnToDeployed) {
            setRuntimeMissionIntent(
                state,
                object,
                RuntimeMissionIntent::Moving,
                "attackChaseStep"
            );
        }
        state.simulationStatus =
            "chasing #"
            + std::to_string(behavior.targetObjectId)
            + " with #"
            + std::to_string(object.instanceId);
        ++state.actionSummary.movedThisTick;
        return true;
    }

    if (omniCrushPursuit) {
        behavior.state = RuntimeBehaviorState::Chasing;
        return false; // The fixed-tick contact pass owns the overrun death.
    }

    // Aircraft spawners maintain a target for the carrier lifecycle. The real
    // child fires its own weapon; the parent emits no virtual projectile.
    if (weapon->spawner
        && runtimeSpawnedMissileProfileForWeapon(state, object, *weapon) == nullptr) {
        behavior.path.clear();
        behavior.pathRequestPending = false;
        behavior.state = RuntimeBehaviorState::Cooldown;
        object.mission = "Attacking";
        return true;
    }

    if (!delayedFirePending) {
        if (behavior.returnToDeployed) {
            setRuntimeMissionIntent(
                state,
                object,
                RuntimeMissionIntent::Deployed,
                "attackInRangeDeployed"
            );
        } else {
            setRuntimeMissionIntent(
                state,
                object,
                RuntimeMissionIntent::Idle,
                "attackInRange"
            );
        }
    }

    if (!updateRuntimeWeaponFacingToward(state, object, *target, behavior.forceFire)) {
        behavior.state = RuntimeBehaviorState::Cooldown;
        state.simulationStatus =
            "turret turning #"
            + std::to_string(object.instanceId)
            + " -> #"
            + std::to_string(target->instanceId);
        return true;
    }

    // The authoritative object reload begins at the final round of a volley.
    // Mirror that deadline into the behavior after pending rounds complete.
    if (!delayedFirePending)
        behavior.weaponCooldownTicks = firingCooldownTicks;

    if (weaponProfile.warhead && weaponProfile.warhead->temporal
        && state.specialMechanisms.hasTemporalAttacker(object.instanceId, target->instanceId)) {
        behavior.state = RuntimeBehaviorState::Attacking;
        object.mission = object.kind == WorldObjectKind::Infantry
            ? runtimeWeaponFiringPose(state, object, weaponProfile).mission : "Attacking";
        return false;
    }

    if (firingCooldownTicks > 0
        || behavior.weaponCooldownTicks > 0) {
        behavior.state = RuntimeBehaviorState::Cooldown;
        if (!delayedFirePending) {
            if (behavior.returnToDeployed) {
                setRuntimeMissionIntent(
                    state,
                    object,
                    RuntimeMissionIntent::Deployed,
                    "attackCooldownDeployed"
                );
            } else {
                setRuntimeMissionIntent(
                    state,
                    object,
                    RuntimeMissionIntent::Idle,
                    "attackCooldown"
                );
            }
        }
        // A live temporal ray/designator still emits during reload. Keep its
        // real weapon pose visible while the saved attack order owns the ray.
        if (object.kind == WorldObjectKind::Infantry && weaponProfile.warhead
            && (weaponProfile.warhead->airstrike || weaponProfile.warhead->temporal))
            object.mission = runtimeWeaponFiringPose(state, object, weaponProfile).mission;
        if (firingCooldownTicks > 0) {
            behavior.weaponCooldownTicks =
                firingCooldownTicks;
        } else {
            --behavior.weaponCooldownTicks;
        }
        return kirovMovementChanged;
    }

    if (weapon->drainWeapon) return beginRuntimeDiskDrain(state, object, *target, *weapon, behavior);
    if (!delayedFirePending) {
        const int prefireDelay = runtimeWeaponPrefireDelayTicks(
            state,
            object,
            *weapon
        );
        if (prefireDelay > 0) {
            if (object.kind == WorldObjectKind::Structure) {
                object.chargeTurretStartTick = state.tick;
            }
            behavior.weaponCooldownTicks = prefireDelay;
            firingCooldownTicks = 0;
            behavior.state = RuntimeBehaviorState::Cooldown;
            object.mission = behavior.returnToDeployed
                ? "DeployedDelayedFire"
                : "DelayedFire";
            clearAttackMovePose();
            state.simulationStatus =
                "prefire #"
                + std::to_string(object.instanceId)
                + " -> #"
                + std::to_string(target->instanceId)
                + " " + weapon->id
                + " ticks " + std::to_string(prefireDelay);
            return true;
        }
    }

    const bool parasiteDeclaration = weapon->limboLaunch
        || (weaponProfile.warhead != nullptr
            && weaponProfile.warhead->parasite);
    const RuntimePrismSupportResult prismSupport = parasiteDeclaration
        ? RuntimePrismSupportResult{}
        : applyRuntimePrismSupport(state, object, *target, *weapon);
    const double veteranDamageMultiplier =
        runtimeVeterancyAbilityMultiplier(
            state,
            object,
            VeteranAbility::Firepower
        );
    const double outgoingDamageMultiplier = veteranDamageMultiplier
        * prismSupport.outgoingDamageMultiplier;
    [[maybe_unused]] const int damage = weapon->areaFire
        ? static_cast<int>(std::lround(
            static_cast<double>(weapon->damage)
                * outgoingDamageMultiplier
        ))
        : runtimeWeaponDamageAgainst(
            state,
            *target,
            *weapon,
            0.0,
            outgoingDamageMultiplier
        );
    if (parasiteDeclaration) {
        const bool launched = launchRuntimeParasiteProjectile(
            state,
            object,
            *target,
            *weapon,
            weaponProfile
        );
        if (launched) {
            ensureRuntimeSpecialMechanismsInitialized(state);
            state.specialMechanisms.notifyActivity(
                object.instanceId,
                state.tick,
                "fire",
                runtimeCamouflageFakeBlinkTicks(state, weapon->disguiseFakeBlinkTime)
            );
            ensureRuntimeSpiesInitialized(state);
            const RuntimeSpyDisguiseResult firedDisguise =
                state.spies.notifyFired(
                    makeRuntimeSpyContext(state),
                    object.instanceId,
                    state.tick
                );
            (void)applyRuntimeSpyActions(
                state,
                firedDisguise.actions
            );
            if (weapon->fireOnce) {
                behavior.path.clear();
                behavior.pathRequestPending = false;
                behavior.targetObjectId = -1;
                behavior.attackBridge = false;
                behavior.attackGround = false;
                behavior.forceFire = false;
                behavior.weaponCooldownTicks = runtimeWeaponCooldownTicks(
                    state,
                    object,
                    *weapon
                );
                firingCooldownTicks =
                    behavior.weaponCooldownTicks;
                behavior.state = RuntimeBehaviorState::Cooldown;
                object.mission = behavior.returnToDeployed
                    ? runtimeMissionName(RuntimeMissionIntent::Deployed)
                    : (behavior.autoAcquired
                        ? "Guard"
                        : runtimeMissionName(RuntimeMissionIntent::Idle));
                ++state.actionSummary.completedCommands;
            }
            ++state.actionSummary.attacksThisTick;
            return true;
        }
        behavior.path.clear();
        behavior.pathRequestPending = false;
        behavior.state = RuntimeBehaviorState::Complete;
        object.mission = behavior.autoAcquired
            ? "Guard"
            : runtimeMissionName(RuntimeMissionIntent::Idle);
        ++state.actionSummary.completedCommands;
        return true;
    }
    if (weapon->decloakToFire) {
        runtimeDecloakObject(state, object, "fire");
    }
    const std::size_t mainProjectileStart = state.projectiles.size();
    if (runtimeSpawnedMissileProfileForWeapon(
            state,
            object,
            *weapon
        ) == nullptr) {
        // An area pulse uses its caster as an internal aim point. That does
        // not authorize friendly fire against everyone in the blast radius.
        beginRuntimeCombatBurst(state, object, *target, *weapon, weaponProfile,
            outgoingDamageMultiplier, -1, behavior.forceFire || (alliedTarget && !areaFireSelf)
                || behavior.attackGround || behavior.attackBridge);
    } else if (!launchRuntimeSpawnedMissileVolley(state, object, *target,
            *weapon, outgoingDamageMultiplier)) {
        // The previous volley may still be active, or slots may be
        // regenerating. A blocked launch spends no ammo or new ROF cycle.
        behavior.state = RuntimeBehaviorState::Cooldown;
        return false;
    }
    // A torpedo may explicitly keep other cloaked units hidden. Submarines
    // still enter their combat-visible surface state after a real discharge.
    runtimeRevealSubmarineForActivity(state, object, "submarineFire");
    if (!prismSupport.supporterIds.empty()) {
        for (std::size_t index = mainProjectileStart;
             index < state.projectiles.size();
             ++index) {
            RuntimeProjectileEffect& projectile = state.projectiles[index];
            if (projectile.attackerObjectId == object.instanceId
                && toLowerAsciiLocal(trimAsciiLocal(projectile.weaponId))
                    == toLowerAsciiLocal(trimAsciiLocal(weapon->id))
                && projectile.laser) {
                // Each contributing tower widens this shot. Keep the result
                // on the projectile so its render actor and saved visual agree.
                projectile.laserThickness = runtimePrismBoostedBeamThickness(
                    projectile.laserThickness, prismSupport.supporterIds.size());
            }
        }
    }
    bool magazineDepleted = false;
    const auto carrierMagazine = state.specialMechanisms.carrierChildren().find(object.instanceId);
    const auto* magazineParent = carrierMagazine == state.specialMechanisms.carrierChildren().end()
        ? nullptr : findObjectById(state, carrierMagazine->second.parentObjectId);
    // Sortie ammunition is independent of the carrier's deck-flight motion.
    const bool threeRoundAircraftMagazine = object.alive && magazineParent && magazineParent->alive
        && ((toUpperAsciiLocal(magazineParent->type) == "CARRIER"
                && toUpperAsciiLocal(object.type) == "HORNET")
            || (toUpperAsciiLocal(magazineParent->type) == "DEST"
                && toUpperAsciiLocal(object.type) == "ASW"));
    const bool carrierMagazineHasMoreRounds = threeRoundAircraftMagazine
        && carrierMagazine != state.specialMechanisms.carrierChildren().end()
        && carrierMagazine->second.sortieShotsRemaining > 1;
    if (ammoRules != nullptr && ammoRules->ammo >= 0 && !carrierMagazineHasMoreRounds) {
        const int ammoBefore = std::clamp(
            object.currentAmmo,
            0,
            ammoRules->ammo
        );
        object.currentAmmo = std::max(0, ammoBefore - 1);
        magazineDepleted = object.currentAmmo == 0;
        state.repaintRequested = true;
    }
    ensureRuntimeSpecialMechanismsInitialized(state);
    state.specialMechanisms.notifyActivity(
        object.instanceId,
        state.tick,
        "fire",
        runtimeCamouflageFakeBlinkTicks(state, weapon->disguiseFakeBlinkTime)
    );
    ensureRuntimeSpiesInitialized(state);
    const RuntimeSpyDisguiseResult firedDisguise = state.spies.notifyFired(
        makeRuntimeSpyContext(state),
        object.instanceId,
        state.tick
    );
    (void)applyRuntimeSpyActions(state, firedDisguise.actions);
    const bool carrierChild =
        state.specialMechanisms.isCarrierChild(object.instanceId);
    const bool fireOnceComplete = weapon->fireOnce;
    if (carrierChild) {
        state.specialMechanisms.notifyCarrierChildFired(
            object.instanceId,
            state.tick,
            runtimeSpecialEventSink(state)
        );
    }
    const bool carrierReturning = carrierChild
        && state.specialMechanisms.carrierChildren().at(object.instanceId).phase == CarrierChildPhase::Returning;
    triggerRuntimeWeaponVisuals(state, object);
    if (carrierReturning) {
        behavior.state = RuntimeBehaviorState::Complete;
        object.mission = "CarrierReturning";
        clearAttackMovePose();
    } else if (magazineDepleted || fireOnceComplete) {
        behavior.state = fireOnceComplete
            ? RuntimeBehaviorState::Cooldown
            : RuntimeBehaviorState::Complete;
        if (fireOnceComplete) {
            behavior.path.clear();
            behavior.pathRequestPending = false;
            behavior.targetObjectId = -1;
            behavior.attackBridge = false;
            behavior.attackGround = false;
            behavior.forceFire = false;
        }
        object.mission = weapon->areaFire && behavior.returnToDeployed
            ? "DeployedAreaFireComplete"
            : (behavior.returnToDeployed
                ? runtimeMissionName(RuntimeMissionIntent::Deployed)
            : (behavior.autoAcquired
                ? "Guard"
                : runtimeMissionName(RuntimeMissionIntent::Idle)));
        clearAttackMovePose();
        ++state.actionSummary.completedCommands;
    } else if (behavior.returnToDeployed) {
        object.mission = "DeployedFire";
    } else {
        setRuntimeMissionIntent(
            state,
            object,
            RuntimeMissionIntent::Attacking,
            "attackFire"
        );
        if (object.kind == WorldObjectKind::Infantry)
            object.mission = runtimeWeaponFiringPose(state, object, weaponProfile).mission;
    }
    behavior.weaponCooldownTicks = runtimeWeaponCooldownTicks(
        state,
        object,
        *weapon
    );
    firingCooldownTicks = behavior.weaponCooldownTicks;
    if (!carrierReturning && !magazineDepleted && !fireOnceComplete) {
        behavior.state = RuntimeBehaviorState::Attacking;
    }
    state.simulationStatus =
        "fire #"
        + std::to_string(object.instanceId)
        + " -> #"
        + std::to_string(target->instanceId)
        + " "
        + weapon->id
        + " slot "
        + runtimeWeaponSlotName(weaponProfile.slot)
        + " projectile "
        + formatMaybe(weaponProfile.projectileId)
        + " warhead "
        + formatMaybe(weaponProfile.warheadId)
        + (prismSupport.supporterIds.empty()
            ? ""
            : " prism support "
                + std::to_string(prismSupport.supporterIds.size()))
        + (magazineDepleted ? " magazine empty" : "")
        + (fireOnceComplete ? " fire once" : "");
    ++state.actionSummary.attacksThisTick;

    if (weapon->suicide) {
        behavior.state = RuntimeBehaviorState::Complete;
        state.simulationStatus += " suicide";
        RuntimeObjectExitRequest objectExit;
        objectExit.objectId = object.instanceId;
        objectExit.kind = RuntimeObjectExitKind::Destroyed;
        objectExit.tick = state.tick;
        objectExit.mission = "Destroyed";
        objectExit.reason = "weapon suicide";
        objectExit.suppressCrew = true;
        objectExit.sourceObjectId = object.instanceId;
        objectExit.sourceOwner = object.owner;
        (void)queueRuntimeObjectExit(state, std::move(objectExit));
    }

    return true;
}

bool runtimeGattlingFiringRunActive(const RuntimeGameSessionState& state, int id) {
    const auto* object = findObjectById(state, id);
    const auto order = state.behaviors.find(id);
    if (!object || !object->alive
        || !runtimeCachedCapabilityProfileForObject(state, *object).gattling
        || !runtimeCombatCanAct(state, *object)
        || !runtimeObjectHasAttackAmmo(state.rules, *object)
        || order == state.behaviors.end() || order->second.kind != RuntimeBehaviorKind::Attack
        || order->second.moveCooldownTicks != 0 || object->renderMoveActive) return false;
    const auto savedHeat = state.gattlingWeaponTimers.find(id);
    // Ordinary saves reconstruct Attack as Queued before its first update.
    // An existing positive heat value still belongs to that valid firing run.
    const bool resumingHeat = order->second.state == RuntimeBehaviorState::Queued
        && savedHeat != state.gattlingWeaponTimers.end() && savedHeat->second > 0;
    if (!resumingHeat && order->second.state != RuntimeBehaviorState::Attacking
        && order->second.state != RuntimeBehaviorState::Cooldown) return false;
    const auto& behavior = order->second;
    std::optional<WorldObjectInstance> terrain;
    if (behavior.attackGround)
        terrain = runtimeGroundAttackTargetObject(state, behavior.attackBridgeCell);
    else if (behavior.attackBridge)
        terrain = runtimeBridgeAttackTargetObject(state, behavior.attackBridgeCell, 0);
    const auto* target = terrain ? &*terrain : findObjectById(state, behavior.targetObjectId);
    if (!target || !target->alive) return false;
    const auto profile = runtimeWeaponProfileForObject(state, *object, target, behavior.forceFire);
    const int facing = runtimeFacingFromDelta(target->x - object->x, target->y - object->y);
    return profile.weapon
        && (profile.weapon->damage > 0 || runtimeWeaponHasAttachEffect(state.rules, profile.weapon))
        && runtimeWeaponHasLineOfSight(state, *object, *target, *profile.weapon)
        && runtimeWeaponDistanceToTarget(state, *object, *target, *profile.weapon)
            <= runtimeEffectiveWeaponRange(state, *object, *target, *profile.weapon)
        && (facing < 0 || runtimeWeaponFiringFacing(state, *object, *target) == facing);
}

bool advanceRuntimeGattlingWeaponTimers(RuntimeGameSessionState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeGattlingWeaponTimers", state.tick);
    std::unordered_set<int> trackedIds;
    for (const auto& [id, timer] : state.gattlingWeaponTimers) {
        (void)timer;
        trackedIds.insert(id);
    }
    for (const auto& [id, behavior] : state.behaviors) {
        if (behavior.kind == RuntimeBehaviorKind::Attack) {
            const auto* object = findObjectById(state, id);
            if (object && runtimeCachedCapabilityProfileForObject(state, *object).gattling)
                trackedIds.insert(id);
        }
    }
    // Weapon stages advance once per original rule frame.
    const int elapsed = runtimeRuleFramesElapsed(state.tick, state.config.fixedTickHz);
    bool changed = false;
    for (int id : trackedIds) {
        const auto* object = findObjectById(state, id);
        if (!runtimeGattlingFiringRunActive(state, id)) {
            // A pause, lost target, movement, obstruction or power loss ends
            // this firing run. Cooldown between shots in the same run does not.
            changed = state.gattlingWeaponTimers.erase(id) > 0 || changed;
            continue;
        }
        if (elapsed == 0) continue;
        const auto& cached = runtimeCachedCapabilityProfileForObject(state, *object);
        int& timer = state.gattlingWeaponTimers[id];
        const int previous = timer;
        timer = static_cast<int>(std::min<long long>(runtimeGattlingTimerCap(cached, object->veterancy >= 2),
            static_cast<long long>(timer) + static_cast<long long>(std::max(1, cached.weaponRateUp)) * elapsed));
        changed = timer != previous || changed;
    }
    return changed;
}

bool advanceRuntimeMindControlOverload(RuntimeProductionHostState& state, const SpecialEventSink& sink) {
    const auto pulses = state.specialMechanisms.tickMindControlOverload(
        state.world.getObjects(), state.rules, state.tick, state.config.fixedTickHz);
    const auto& rules = state.rules.getSpecialMechanismRules();
    bool changed = false;
    for (const auto& pulse : pulses) {
        auto* controller = findMutableObjectById(state, pulse.controllerObjectId);
        if (controller == nullptr || !controller->alive) continue;
        int applied = 0;
        if (!runtimeObjectIgnoresDamage(state, *controller)
            && !(state.superWeaponsInitialized
                && state.superWeapons.isObjectProtected(controller->instanceId, state.tick))) {
            // Retail calls ReceiveDamage on the controller with C4Warhead and
            // no attacker. This damages only the controller, never its victims.
            applied = runtimeAuthoredDamageAgainst(state, *controller, pulse.damage, rules.c4Warhead);
            controller->health = std::max(0, controller->health - applied);
            if (applied > 0) runtimeRevealObjectAfterDamage(state, *controller, "mindControlOverload");
            changed = changed || applied > 0;
        }
        if (pulse.playSound && !rules.masterMindOverloadDeathSound.empty())
            emitRuntimeSound(state, "sound:" + rules.masterMindOverloadDeathSound, runtimeSpatialAudioPositionForObject(*controller));
        if (sink) sink({"MindControlOverload", state.tick, controller->instanceId, -1, applied,
            {controller->x, controller->y}, "controlled=" + std::to_string(pulse.controlledCount)});
        if (controller->health <= 0) {
            RuntimeObjectExitRequest exit;
            exit.objectId = controller->instanceId;
            exit.kind = RuntimeObjectExitKind::Destroyed;
            exit.tick = state.tick;
            exit.mission = "Destroyed";
            exit.reason = "mind-control overload";
            (void)queueRuntimeObjectExit(state, std::move(exit));
        }
    }
    return drainRuntimeObjectExitQueue(state) || changed;
}

bool advanceRuntimeSquidGrappleWater(RuntimeProductionHostState& state) {
    bool changed = false;
    for (const auto& [targetId, link] : state.specialMechanisms.parasiteLinks()) {
        if (!link.organic || toLowerAsciiLocal(link.sourceTypeId) != "sqd") continue;
        const int age = state.tick - link.attachedTick;
        if (age <= 0) continue;
        const int before = runtimeSquidGrappleNativeAge(age - 1, state.config.fixedTickHz);
        const int now = runtimeSquidGrappleNativeAge(age, state.config.fixedTickHz);
        // Retail state 4 spawns three splashes when a ten-pose rocking strip
        // finishes. Integer boundary crossing also works at non-multiple Hz.
        const int cycle = (now - 30) / 40;
        if (now < 70 || before >= 30 + cycle * 40) continue;
        const auto* target = findObjectById(state, targetId);
        const auto* unit = target ? findRuntimeUnitType(state.rules, *target) : nullptr;
        if (!target || !unit || !unit->naval || unit->organic) continue;
        auto splashes = splitRuntimeTokenList(state.rules.getAudioVisualValue("SplashList"));
        splashes.erase(std::remove_if(splashes.begin(), splashes.end(), [&](const auto& id) {
            return state.art.findArtType(id) == nullptr;
        }), splashes.end());
        if (splashes.empty()) continue;
        std::uint64_t seed = runtimeStableHashString("SquidGrappleSplash/1");
        runtimeStableHashMix(seed, link.sourceObjectId);
        runtimeStableHashMix(seed, targetId);
        runtimeStableHashMix(seed, link.projectileId);
        runtimeStableHashMix(seed, cycle);
        const double yaw = (worldObjectArtFacingBucket(target->facing, 256) - 64)
            * 6.283185307179586 / 256;
        for (int slot = 0; slot < 3; ++slot) {
            runtimeStableHashMix(seed, slot);
            // Retail offsets: -128, 0, +128 longitudinal and +/-64 lateral
            // leptons, rotated with the host. Positions remain on the water.
            const double forward = (slot - 1) * 0.5;
            const double lateral = (seed & 1U) ? -0.25 : 0.25;
            RuntimeProjectileTargetPosition position;
            position.x = target->x + forward * std::cos(yaw) - lateral * std::sin(yaw);
            position.y = target->y + forward * std::sin(yaw) + lateral * std::cos(yaw);
            position.level = target->level;
            position.cell = {static_cast<int>(std::lround(position.x)), static_cast<int>(std::lround(position.y))};
            const std::string& image = splashes[(seed >> 1U) % splashes.size()];
            spawnRuntimeImpact(state, position.cell, position.level, false, image,
                "", 0, 0.0, false, "", nullptr, &position);
            auto& splash = state.impactEffects.back();
            splash.lifecycleState = "SquidSplash";
            splash.renderIntent = "squidSplash";
            // Snapshot transfer retains effect intent IDs, while discarding
            // cached image/frame fields. This intent is an animation, not a
            // damaging warhead, and reconstructs the same chosen resource.
            splash.warheadImpactIntentId = image;
            splash.attackerObjectId = link.sourceObjectId;
            splash.targetObjectId = targetId;
            splash.owner = link.owner;
            initializeRuntimeImpactAnimationTimeline(state, splash, true);
            changed = true;
        }
    }
    return changed;
}

void clearRuntimeCarrierAttackPreservingPose(RuntimeProductionHostState& state,
    WorldObjectInstance& child) {
    if (!state.behaviors.contains(child.instanceId)) return;
    const double x = runtimeObjectRenderX(child), y = runtimeObjectRenderY(child);
    const double level = child.hasRenderPosition ? child.renderLevel : child.level;
    eraseRuntimeBehavior(state, child.instanceId);
    child.hasRenderPosition = true;
    child.renderMoveActive = true;
    child.renderX = x; child.renderY = y; child.renderLevel = level;
    child.renderOnBridge = false;
}

std::optional<WorldObjectInstance> runtimeCarrierAircraftAttackTarget(
    const RuntimeGameSessionState& state, const RuntimeObjectBehavior& order, int targetId) {
    std::optional<WorldObjectInstance> target;
    if (order.attackGround) {
        target = runtimeGroundAttackTargetObject(state, order.attackBridgeCell);
    } else if (order.attackBridge) {
        target = runtimeBridgeAttackTargetObject(state, order.attackBridgeCell);
    } else if (const auto* object = findObjectById(state, targetId)) {
        target = *object;
    }
    return target && target->instanceId == targetId ? target : std::nullopt;
}

bool synchronizeRuntimeCarrierAircraftOrders(RuntimeProductionHostState& state,
    const SpecialEventSink& sink) {
    bool changed = false;
    for (const auto& [childId, sortie] : state.specialMechanisms.carrierChildren()) {
        auto* child = findMutableObjectById(state, childId);
        if (!child || !child->alive || state.rules.findSpawnedMissileProfile(child->type)) continue;
        if (sortie.phase == CarrierChildPhase::Returning) {
            if (state.behaviors.contains(childId)) {
                clearRuntimeCarrierAttackPreservingPose(state, *child);
                child->mission = "CarrierReturning";
                changed = true;
            }
            continue;
        }
        if (sortie.phase != CarrierChildPhase::Launching
            && sortie.phase != CarrierChildPhase::Attacking) continue;
        const auto parentOrder = state.behaviors.find(sortie.parentObjectId);
        const auto childOrder = state.behaviors.find(childId);
        const auto target = parentOrder == state.behaviors.end()
            ? std::nullopt : runtimeCarrierAircraftAttackTarget(
                state, parentOrder->second, sortie.targetObjectId);
        bool recall = !target || !target->alive || parentOrder == state.behaviors.end()
            || parentOrder->second.kind != RuntimeBehaviorKind::Attack
            || childOrder == state.behaviors.end()
            || childOrder->second.kind != RuntimeBehaviorKind::Attack;
        if (!recall) {
            auto& behavior = childOrder->second;
            const auto& parent = parentOrder->second;
            const bool orderChanged = behavior.targetObjectId != sortie.targetObjectId
                || behavior.forceFire != parent.forceFire
                || behavior.autoAcquired != parent.autoAcquired
                || behavior.attackGround != parent.attackGround
                || behavior.attackBridge != parent.attackBridge
                || behavior.attackBridgeCell.x != parent.attackBridgeCell.x
                || behavior.attackBridgeCell.y != parent.attackBridgeCell.y;
            if (orderChanged) {
                behavior.path.clear(); behavior.pathRequestPending = false;
                if (behavior.state == RuntimeBehaviorState::Complete)
                    behavior.state = RuntimeBehaviorState::Queued;
                changed = true;
            }
            // Copy the target and its relation semantics together, after the
            // carrier state machine has selected this tick's parent target.
            behavior.targetObjectId = sortie.targetObjectId;
            behavior.forceFire = parent.forceFire;
            behavior.autoAcquired = parent.autoAcquired;
            behavior.attackGround = parent.attackGround;
            behavior.attackBridge = parent.attackBridge;
            behavior.attackBridgeCell = parent.attackBridgeCell;
            const auto profile = runtimeWeaponProfileForObject(state, *child, &*target, behavior.forceFire);
            recall = behavior.state == RuntimeBehaviorState::Complete
                || !profile.weapon || !runtimeWeaponHasAttackRange(*profile.weapon)
                || !runtimeObjectHasAttackAmmo(state.rules, *child);
        }
        if (recall) {
            changed = state.specialMechanisms.requestCarrierChildReturn(
                childId, state.tick, sink, "aircraft attack unavailable") || changed;
            clearRuntimeCarrierAttackPreservingPose(state, *child);
            child->mission = "CarrierReturning";
        }
    }
    return changed;
}

bool runtimeCarrierHasDeckFlight(const RuntimeGameSessionState& state,
    const WorldObjectInstance& child) {
    const auto sortie = state.specialMechanisms.carrierChildren().find(child.instanceId);
    const auto* parent = sortie == state.specialMechanisms.carrierChildren().end()
        ? nullptr : findObjectById(state, sortie->second.parentObjectId);
    return child.alive && toUpperAsciiLocal(child.type) == "HORNET" && parent && parent->alive
        && toUpperAsciiLocal(parent->type) == "CARRIER";
}

bool runtimeCarrierHasContinuousAttackFlight(const RuntimeGameSessionState& state,
    const WorldObjectInstance& child) {
    const auto sortie = state.specialMechanisms.carrierChildren().find(child.instanceId);
    const auto* unit = findRuntimeUnitType(state.rules, child);
    return child.airborne && runtimeCarrierHasDeckFlight(state, child)
        && sortie != state.specialMechanisms.carrierChildren().end()
        && sortie->second.phase == CarrierChildPhase::Attacking
        && unit && runtimeUnitUsesAirMovement(*unit) && !runtimeUnitUsesJumpjetMovement(*unit)
        && !unit->missileSpawn && !state.rules.findSpawnedMissileProfile(child.type);
}

std::array<double, 3> runtimeCarrierDeckMouth(const WorldObjectInstance& parent) {
    constexpr double modelUnitsPerCell = 0.18 * 256.0;
    constexpr double x = 4.485871315002441 / modelUnitsPerCell;
    constexpr double y = 0.9853174076523885 / modelUnitsPerCell;
    constexpr double z = (7.883922510390932 / 0.18) * (2.449489742783178 / 256.0);
    const auto forward = runtimeWorldForwardForFacing(std::max(0, parent.facing));
    return {runtimeObjectRenderX(parent) + forward[0] * x - forward[1] * y,
        runtimeObjectRenderY(parent) + forward[1] * x + forward[0] * y,
        runtimeObjectRenderLevel(parent) + z};
}

bool runtimeCarrierAircraftHasDeckProtection(const RuntimeGameSessionState& state,
    const WorldObjectInstance& child) {
    if (!child.alive || child.kind != WorldObjectKind::Unit) return false;
    const auto registered = state.specialMechanisms.carrierChildren().find(child.instanceId);
    if (registered == state.specialMechanisms.carrierChildren().end()) return false;
    const auto& sortie = registered->second;
    const auto parentState = state.specialMechanisms.carrierParents().find(sortie.parentObjectId);
    const auto* parent = findObjectById(state, sortie.parentObjectId);
    if (!parent || !parent->alive || parent->kind != WorldObjectKind::Unit
        || parentState == state.specialMechanisms.carrierParents().end()
        || sortie.childObjectId != child.instanceId || sortie.slot < 0
        || sortie.slot >= parentState->second.capacity) return false;
    const auto slot = parentState->second.childBySlot.find(sortie.slot);
    if (slot == parentState->second.childBySlot.end() || slot->second != child.instanceId) return false;
    const std::string childType = toUpperAsciiLocal(child.type);
    const std::string parentType = toUpperAsciiLocal(parent->type);
    // Unreleased spawned missiles remain cargo of the exact registered slot.
    // Separation removes this graph before the independent airborne object flies.
    const auto* childUnit = findRuntimeUnitType(state.rules, child);
    const auto* parentUnit = findRuntimeUnitType(state.rules, *parent);
    if (childUnit && childUnit->missileSpawn
        && state.rules.findSpawnedMissileProfile(child.type)
        && parentUnit && parentUnit->spawnsNumber > sortie.slot
        && toUpperAsciiLocal(parentUnit->spawns) == childType
        && toUpperAsciiLocal(parentState->second.childType) == childType) {
        if (sortie.phase != CarrierChildPhase::Docked || child.airborne
            || child.dockedToObjectId != parent->instanceId
            || child.dockIndex != sortie.slot) return false;
        const auto preparing = state.specialMechanisms.spawnedMissiles().find(child.instanceId);
        return preparing == state.specialMechanisms.spawnedMissiles().end()
            || (!preparing->second.released
                && preparing->second.objectId == child.instanceId
                && preparing->second.parentObjectId == parent->instanceId
                && preparing->second.slot == sortie.slot
                && toUpperAsciiLocal(preparing->second.typeId) == childType);
    }
    const bool hornet = childType == "HORNET" && parentType == "CARRIER";
    if (!hornet && !(childType == "ASW" && parentType == "DEST")) return false;
    if (!child.airborne) return sortie.phase == CarrierChildPhase::Docked
        || sortie.phase == CarrierChildPhase::Launching;
    if (!hornet || sortie.phase != CarrierChildPhase::Launching || !child.hasRenderPosition) return false;
    const auto mouth = runtimeCarrierDeckMouth(*parent);
    return std::abs(runtimeObjectRenderLevel(child) - mouth[2]) <= 1e-10
        && std::hypot(runtimeObjectRenderX(child) - mouth[0],
            runtimeObjectRenderY(child) - mouth[1]) <= 0.20;
}

void applyRuntimeCarrierDeckFlightPresentation(const RuntimeGameSessionState& state,
    WorldObjectInstance& child) {
    if (!runtimeCarrierHasDeckFlight(state, child)) return;
    const auto& sortie = state.specialMechanisms.carrierChildren().at(child.instanceId);
    const auto* parent = findObjectById(state, sortie.parentObjectId);
    if (!child.airborne || sortie.phase == CarrierChildPhase::Docked) {
        child.aircraftDockFlightProgress = -1.0;
        return;
    }
    const double deck = runtimeCarrierDeckMouth(*parent)[2];
    child.aircraftDockFlightProgress = std::clamp(
        (runtimeObjectRenderLevel(child) - deck) / std::max(0.001, child.flightLevel - deck), 0.0, 1.0);
    if (child.hasRenderPosition) child.level = static_cast<int>(std::lround(child.renderLevel));
}

void setRuntimeCarrierDeckFlightPose(const RuntimeGameSessionState& state, WorldObjectInstance& child,
    double x, double y, double level, bool moving) {
    const double distance = std::hypot(x - runtimeObjectRenderX(child), y - runtimeObjectRenderY(child));
    child.x = static_cast<int>(std::lround(x)); child.y = static_cast<int>(std::lround(y));
    child.cell = runtimeCellKey(child.x, child.y);
    child.level = static_cast<int>(std::lround(level));
    child.hasRenderPosition = true; child.renderMoveActive = moving;
    child.renderX = x; child.renderY = y; child.renderLevel = level;
    child.onBridge = child.renderOnBridge = false; child.subCell = -1;
    child.movementAnimationDistance = std::fmod(child.movementAnimationDistance + distance, 1024.0);
    applyRuntimeCarrierDeckFlightPresentation(state, child);
}

double runtimeCarrierVerticalStep(const RuntimeGameSessionState& state, double remaining) {
    if (remaining <= 0.0) return 0.0;
    const auto distance = std::max<std::int64_t>(1,
        static_cast<std::int64_t>(std::llround(remaining * runtimeAircraftCellQ16)));
    return std::min(remaining, double(runtimeFixedWingVerticalStep(state, distance)) / runtimeAircraftCellQ16);
}

void beginRuntimeCarrierDeckDeparture(RuntimeGameSessionState& state, WorldObjectInstance& child,
    const WorldObjectInstance& parent) {
    const auto mouth = runtimeCarrierDeckMouth(parent);
    if (!child.airborne) queueRuntimeAircraftAuxSound(state, child, true);
    child.airborne = true; child.flightLevel = std::max(runtimeAircraftCruiseLevel(state),
        static_cast<int>(std::ceil(mouth[2])) + 1);
    child.dockedToObjectId = child.dockIndex = -1;
    child.facing = parent.facing; child.bodyFacingClock = {};
    child.mission = "CarrierLaunching";
    setRuntimeCarrierDeckFlightPose(state, child, mouth[0], mouth[1], mouth[2], true);
}

bool advanceRuntimeCarrierDeckDeparture(RuntimeGameSessionState& state, WorldObjectInstance& child,
    const WorldObjectInstance& parent, const SpecialEventSink& sink) {
    const auto mouth = runtimeCarrierDeckMouth(parent);
    const double before = runtimeObjectRenderLevel(child);
    const double level = std::min(double(child.flightLevel), before
        + runtimeCarrierVerticalStep(state, child.flightLevel - before));
    setRuntimeCarrierDeckFlightPose(state, child, mouth[0], mouth[1], level, true);
    child.mission = "CarrierLaunching";
    if (level >= child.flightLevel) {
        const auto& own = state.specialMechanisms.carrierChildren().at(child.instanceId);
        // Keep the authored ascent; release horizontal flight only after an
        // earlier slot clears the mouth. Existing saved phases/poses own it.
        for (const auto& [id, sortie] : state.specialMechanisms.carrierChildren()) {
            if (sortie.parentObjectId != parent.instanceId || sortie.slot >= own.slot
                || (sortie.phase != CarrierChildPhase::Launching
                    && sortie.phase != CarrierChildPhase::Attacking)) continue;
            const auto* earlier = findObjectById(state, id);
            if (earlier && earlier->alive && earlier->airborne
                && std::hypot(runtimeObjectRenderX(*earlier) - mouth[0],
                    runtimeObjectRenderY(*earlier) - mouth[1]) < 1.0) return true;
        }
        state.specialMechanisms.notifyCarrierChildLaunched(child.instanceId, state.tick, sink);
        child.mission = "Attacking";
    }
    return true;
}

bool advanceRuntimeCarrierAircraftToward(RuntimeProductionHostState& state, WorldObjectInstance& child,
    RuntimeObjectBehavior& behavior, double goalX, double goalY) {
    if (!runtimeCarrierHasContinuousAttackFlight(state, child)) return false;
    const double x = runtimeObjectRenderX(child), y = runtimeObjectRenderY(child);
    const double dx = std::clamp(goalX, 0.0, 511.999) - x;
    const double dy = std::clamp(goalY, 0.0, 511.999) - y;
    const double distance = std::hypot(dx, dy);
    double travel = std::min(distance, 1.0 / runtimeFixedWingCruiseInterval(state, child));
    double nextX = x, nextY = y;
    if (distance > 1e-12 && travel > 0.0) {
        const int desired = runtimeFacingFromRenderedDelta(dx, dy);
        child.facing = runtimeAdvanceBodyFacing(state, child, std::max(0, child.facing), desired);
        if (child.facing == desired && !child.bodyFacingClock.active()) {
            nextX += dx * travel / distance; nextY += dy * travel / distance;
        } else {
            constexpr double tau = 6.2831853071795864769;
            const double maxTurn = std::max(1, runtimeBodyTurnStep(state, child))
                * tau / 256.0 * 15.0 / std::max(1, state.config.fixedTickHz);
            travel = std::min(travel, distance * maxTurn * 0.5);
            const double heading = (child.bodyFacingClock.active()
                ? child.bodyFacingClock.current16(state.config.fixedTickHz)
                : std::max(0, child.facing) * 256) * tau / 65536.0;
            nextX += (std::sin(heading) - std::cos(heading)) * 0.7071067811865475244 * travel;
            nextY += (-std::sin(heading) - std::cos(heading)) * 0.7071067811865475244 * travel;
        }
    }
    nextX = std::clamp(nextX, 0.0, 511.999); nextY = std::clamp(nextY, 0.0, 511.999);
    behavior.path.clear(); behavior.pathRequestPending = false; behavior.moveCooldownTicks = 0;
    behavior.moveSegmentFrom = {-1, -1};
    behavior.renderMoveTicksTotal = behavior.renderMoveTicksRemaining = 0;
    const auto previous = runtimeCellPointForObject(child);
    const bool moved = nextX != x || nextY != y;
    setRuntimeCarrierDeckFlightPose(state, child, nextX, nextY, runtimeObjectRenderLevel(child), moved);
    const auto next = runtimeCellPointForObject(child);
    if (next != previous) updateRuntimeTickOccupancyForMove(state, child, previous, next);
    behavior.state = RuntimeBehaviorState::Chasing;
    setRuntimeMissionIntent(state, child, RuntimeMissionIntent::Moving, "continuousCarrierAircraftFlight");
    if (moved) ++state.actionSummary.movedThisTick;
    return moved;
}

bool advanceRuntimeCarrierDeckReturn(RuntimeProductionHostState& state, WorldObjectInstance& child,
    const WorldObjectInstance& parent, int slot, const SpecialEventSink& sink) {
    clearRuntimeCarrierAttackPreservingPose(state, child);
    child.dockedToObjectId = child.dockIndex = -1;
    child.airborne = true; child.mission = "CarrierReturning";
    const auto mouth = runtimeCarrierDeckMouth(parent);
    const double x = runtimeObjectRenderX(child), y = runtimeObjectRenderY(child);
    const double dx = mouth[0] - x, dy = mouth[1] - y, distance = std::hypot(dx, dy);
    const double step = 1.0 / std::max(1, runtimeMovementIntervalTicks(state, child));
    const double travel = std::min(step, distance);
    const double nextX = distance > 1e-12 ? x + dx * travel / distance : mouth[0];
    const double nextY = distance > 1e-12 ? y + dy * travel / distance : mouth[1];
    double level = runtimeObjectRenderLevel(child);
    // A recalled partly emerged aircraft returns from its actual height.
    // No cruise-height reset is allowed at the recall boundary.
    if (distance <= std::max(0.20, step * 1.25)) {
        if (level > mouth[2]) level = std::max(mouth[2], level
            - runtimeCarrierVerticalStep(state, level - mouth[2]));
        else if (level < mouth[2]) level = std::min(mouth[2], level
            + runtimeCarrierVerticalStep(state, mouth[2] - level));
    }
    if (distance > 1e-8) {
        const int facing = runtimeFacingFromDelta(static_cast<int>(std::lround(dx * 1024.0)),
            static_cast<int>(std::lround(dy * 1024.0)));
        if (facing >= 0) child.facing = facing;
    }
    setRuntimeCarrierDeckFlightPose(state, child, nextX, nextY, level, true);
    if (distance <= step + 1e-10 && std::abs(level - mouth[2]) < 1e-10) {
        queueRuntimeAircraftAuxSound(state, child, false, 0);
        child.x = parent.x; child.y = parent.y; child.cell = runtimeCellKey(child.x, child.y);
        child.level = parent.level; child.dockedToObjectId = parent.instanceId; child.dockIndex = slot;
        child.airborne = false; child.flightLevel = 0; child.mission = "Dock";
        child.aircraftDockFlightProgress = -1.0; clearRuntimeRenderPosition(child);
        initializeRuntimeObjectAmmo(child, runtimeAmmoRulesForObject(state.rules, child), true);
        state.specialMechanisms.notifyCarrierChildDocked(child.instanceId, state.tick, sink);
    }
    return true;
}

bool runtimeUsesHomingMissileGuidance(const RuntimeProjectileEffect& shot,
    const ProjectileType& type) {
    return type.rot > 0 && type.ranged && !type.arcing && !type.vertical
        && !type.dropping && !type.inviso && !shot.spawnedMissileDamage
        && !shot.laser && !shot.electricBolt && !shot.voxelDebris.enabled;
}

bool runtimeUsesMissileStartupAcceleration(const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& shot, const ProjectileType& type) {
    const auto* weapon = findRuntimeWeaponType(state.rules, shot.weaponId);
    if (weapon && weapon->diskLaser) return false;
    return type.rot > 0 && (type.ranged || shot.renderIntent == "radialProjectile")
        && !type.arcing && !type.vertical && !type.dropping && !type.inviso
        && !shot.spawnedMissileDamage && !shot.laser && !shot.electricBolt
        && !shot.voxelDebris.enabled && shot.renderIntent != "diskLaserCharge"
        && !runtimeProjectileIsParasiteLeap(state, shot);
}

bool runtimeUsesCompactAegisRadialLaunch(const RuntimeProjectileEffect& shot,
    const ProjectileType& type) {
    if (shot.renderIntent != "radialProjectile" || !shot.launchSourceValidated
        || toUpperAsciiLocal(trimAsciiLocal(shot.launchSourceType)) != "AEGIS"
        || toUpperAsciiLocal(trimAsciiLocal(shot.projectileId)) != "MEDUSAPROJECTILE"
        || toUpperAsciiLocal(trimAsciiLocal(type.id)) != "MEDUSAPROJECTILE") return false;
    const auto weapon = toUpperAsciiLocal(trimAsciiLocal(shot.weaponId));
    return weapon == "MEDUSA" || weapon == "MEDUSAE";
}

bool runtimeUsesCloseAegisRadialLaunch(const RuntimeProjectileEffect& shot,
    const ProjectileType& type) {
    return runtimeUsesCompactAegisRadialLaunch(shot, type)
        && std::hypot(shot.targetX-shot.sourceX, shot.targetY-shot.sourceY) <= 5.0;
}

double runtimeHomingMissileCourseLockRuleFrames(const RuntimeProjectileEffect& shot,
    const ProjectileType& type) {
    const double authored = std::max(0, type.courseLockDuration);
    // A nearby target can lie inside the original turn circle. Start steering
    // after one rule frame, from the direct initial bearing below, instead of
    // letting a high-speed missile circle that target indefinitely.
    return runtimeUsesCompactAegisRadialLaunch(shot, type)
        ? std::min(authored, runtimeUsesCloseAegisRadialLaunch(shot, type) ? 1.0 : 3.0)
        : authored;
}

bool advanceRuntimeHomingMissilePosition(const RuntimeGameSessionState& state,
    RuntimeProjectileEffect& shot, const ProjectileType& type) {
    constexpr double pi = 3.14159265358979323846;
    constexpr double heightPerLepton = runtimeMissileLevelsPerCell / 256.0;
    constexpr double nativeCellHeight = 104.0 * heightPerLepton;
    const double dt = 15.0 / std::max(1, state.config.fixedTickHz);
    const double step = runtimeUsesMissileStartupAcceleration(state, shot, type)
        ? runtimeMissileAccelerationStepCells(shot.speedCellsPerTick, shot.acceleration,
            shot.elapsedTicks, state.config.fixedTickHz)
        : std::max(0.00001, shot.speedCellsPerTick);
    double dx = shot.targetX - shot.currentX;
    double dy = shot.targetY - shot.currentY;
    double dz = (shot.targetZ - shot.currentZ) / runtimeMissileLevelsPerCell;
    double horizontal = std::hypot(dx, dy);
    const double distance = std::hypot(horizontal, dz);
    if (distance <= step + 0.0001) {
        shot.currentX = shot.targetX; shot.currentY = shot.targetY; shot.currentZ = shot.targetZ;
        return true;
    }
    if (!shot.homingInitialized) {
        shot.homingInitialized = true;
        if (shot.renderIntent == "radialProjectile") {
            const double angle = shot.attackerFacing * (2.0 * pi / 256.0);
            const double sx = std::sin(angle), sy = -std::cos(angle);
            shot.homingYaw = std::atan2(sy-sx, sx+sy);
            if (runtimeUsesCloseAegisRadialLaunch(shot, type)) {
                // A close target can be inside even a scaled radial turn
                // circle at a finer clock. Start on its bearing; all later
                // movement and steering still use the authored speed/ROT.
                shot.homingYaw = std::atan2(dy, dx);
            }
        } else shot.homingYaw = std::atan2(dy, dx);
        // Native launch is horizontal unless the height difference exceeds
        // 200 leptons. Legacy saves begin from their existing flight point.
        shot.homingPitch = !type.level && (std::abs(dz) * 256.0 > 200.0 || shot.elapsedTicks > 1)
            ? std::atan2(dz, horizontal) : 0.0;
    }
    const double vx = std::cos(shot.homingYaw) * std::cos(shot.homingPitch);
    const double vy = std::sin(shot.homingYaw) * std::cos(shot.homingPitch);
    shot.currentX += vx * step;
    shot.currentY += vy * step;
    shot.currentZ += std::sin(shot.homingPitch) * step * runtimeMissileLevelsPerCell;
    dx = shot.targetX - shot.currentX; dy = shot.targetY - shot.currentY;
    dz = (shot.targetZ - shot.currentZ) / runtimeMissileLevelsPerCell;
    horizontal = std::hypot(dx, dy);
    const auto turn = [](double current, double desired, double rate) {
        const double delta = std::remainder(desired - current, 2.0 * pi);
        return std::remainder(current + std::clamp(delta, -rate, rate), 2.0 * pi);
    };
    const double rate = std::clamp(type.rot, 0, 127) * (2.0 * pi / 256.0) * dt;
    if (shot.elapsedTicks * dt < runtimeHomingMissileCourseLockRuleFrames(shot, type)) return false;
    shot.homingYaw = turn(shot.homingYaw, std::atan2(dy, dx), rate);
    if (type.level) { shot.homingPitch = 0.0; return false; }
    const auto* target = findObjectById(state, shot.targetObjectId);
    const bool airTarget = target && runtimeObjectIsAirTarget(state, *target);
    if (!airTarget && type.rot > 1 && (type.airburst || horizontal > (type.veryHigh ? 6.0 : 3.0))) {
        // 0x5B2640 samples the ground six native velocity steps ahead.
        const double ahead = step / dt * 6.0;
        const int ground = terrainLevelAtRuntimeCell(state,
            static_cast<int>(std::lround(shot.currentX + vx * ahead)),
            static_cast<int>(std::lround(shot.currentY + vy * ahead)));
        const double cruise = ground + nativeCellHeight * std::min(
            std::floor(std::hypot(horizontal, dz)), type.veryHigh || type.airburst ? 10.0 : 5.0);
        const double heightError = shot.currentZ - cruise;
        // Native 0x5B2744 has both an 18-lepton height correction and pitch
        // guidance. This produces the sloping launch rather than a sine arc.
        if (heightError < -20.0 * heightPerLepton) shot.currentZ += 18.0 * heightPerLepton * dt;
        else if (heightError > 20.0 * heightPerLepton) shot.currentZ -= 18.0 * heightPerLepton * dt;
        const double desiredPitch = heightError < -nativeCellHeight / 2.0 ? pi / 4.0
            : heightError > nativeCellHeight / 2.0 ? -pi / 16.0 : 0.0;
        shot.homingPitch = turn(shot.homingPitch, desiredPitch, rate / 2.0);
    } else {
        shot.homingPitch = turn(shot.homingPitch, std::atan2(dz, horizontal), rate);
    }
    return false;
}

bool advanceRuntimeRangedProjectilePosition(
    const RuntimeGameSessionState& state, RuntimeProjectileEffect& projectile, const ProjectileType& type) {
    if (runtimeProjectileIsParasiteLeap(state, projectile)) {
        // A LimboLaunch body leaps from its actual launch pose to contact.
        // Missile cruise steering and an early proximity detonation would
        // leave a visible gap that the generic terminal interpolation snaps.
        const double dx = projectile.targetX - projectile.currentX;
        const double dy = projectile.targetY - projectile.currentY;
        const double dz = (projectile.targetZ - projectile.currentZ)
            / runtimeMissileLevelsPerCell;
        const double distance = std::hypot(std::hypot(dx, dy), dz);
        const double step = std::max(0.00001, projectile.speedCellsPerTick);
        const bool reached = distance <= step + 1e-9;
        if (distance > 0.0001) {
            // The saved direction also supplies DRONP's Rotates frame, including
            // the last actor where target-current becomes zero after contact.
            projectile.homingInitialized = true;
            projectile.homingYaw = std::atan2(dy, dx);
            projectile.homingPitch = std::atan2(dz, std::hypot(dx, dy));
        }
        const double scale = reached || distance <= 0.0001
            ? 1.0 : step / distance;
        projectile.currentX += dx * scale;
        projectile.currentY += dy * scale;
        projectile.currentZ += dz * scale * runtimeMissileLevelsPerCell;
        const double travelled = std::hypot(
            std::hypot(projectile.currentX - projectile.sourceX,
                projectile.currentY - projectile.sourceY),
            (projectile.currentZ - projectile.sourceZ)
                / runtimeMissileLevelsPerCell);
        const double remaining = reached ? 0.0 : distance - step;
        projectile.progress = reached ? 1.0 : std::clamp(
            travelled / std::max(0.0001, travelled + remaining), 0.0, 1.0);
        projectile.currentLevel = static_cast<int>(std::lround(projectile.currentZ));
        projectile.currentCell = {static_cast<int>(std::lround(projectile.currentX)),
            static_cast<int>(std::lround(projectile.currentY))};
        return reached;
    }
    if (runtimeUsesHomingMissileGuidance(projectile, type)) {
        const bool reached = advanceRuntimeHomingMissilePosition(state, projectile, type);
        projectile.progress = runtimeProjectileProgress(projectile);
        projectile.currentLevel = static_cast<int>(std::lround(projectile.currentZ));
        projectile.currentCell = {static_cast<int>(std::lround(projectile.currentX)),
            static_cast<int>(std::lround(projectile.currentY))};
        return reached;
    }
    const double targetX = projectile.hasTargetPosition
        ? projectile.targetX
        : static_cast<double>(projectile.toCell.x);
    const double targetY = projectile.hasTargetPosition
        ? projectile.targetY
        : static_cast<double>(projectile.toCell.y);
    const double targetZ = projectile.hasTargetPosition
        ? projectile.targetZ
        : static_cast<double>(projectile.toLevel);
    const double dx = targetX - projectile.currentX;
    const double dy = targetY - projectile.currentY;
    const double dz = targetZ - projectile.currentZ;
    const double distance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    const double maximumStep = std::max(0.00001, projectile.speedCellsPerTick);
    const bool reachedTarget = distance <= maximumStep + 0.0001;
    if (reachedTarget || distance <= 0.0001) {
        projectile.currentX = targetX;
        projectile.currentY = targetY;
        projectile.currentZ = targetZ;
    } else {
        const double scale = maximumStep / distance;
        projectile.currentX += dx * scale;
        projectile.currentY += dy * scale;
        projectile.currentZ += dz * scale;
    }
    projectile.progress = runtimeProjectileProgress(projectile);
    projectile.currentLevel = static_cast<int>(std::lround(
        projectile.currentZ
    ));
    projectile.currentCell = {
        static_cast<int>(std::lround(projectile.currentX)),
        static_cast<int>(std::lround(projectile.currentY))
    };
    return reachedTarget;
}

void expireRuntimeProjectileAtCurrentPosition(
    RuntimeProjectileEffect& projectile) {
    projectile.targetObjectId = -1;
    projectile.toCell = projectile.currentCell;
    projectile.hasTargetPosition = true;
    projectile.targetX = projectile.currentX;
    projectile.targetY = projectile.currentY;
    projectile.targetZ = projectile.currentZ;
    projectile.toLevel = projectile.currentLevel;
    projectile.impactTick = projectile.spawnTick + projectile.elapsedTicks;
}

int runtimeProjectileTrailerSeparationTicks(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile) {
    const ProjectileType* projectileType =
        findRuntimeProjectileType(state.rules, projectile.projectileId);
    int authoredFrames = projectileType == nullptr
        ? 0
        : std::max(0, projectileType->trailerSeperation);
    if (authoredFrames <= 0) {
        const std::string artId = projectile.projectileImageId.empty()
            ? projectile.projectileId
            : projectile.projectileImageId;
        const ArtType* projectileArt = state.art.findArtType(artId);
        authoredFrames = projectileArt == nullptr
            ? 0
            : std::max(0, projectileArt->trailerSeperation);
    }
    return authoredFrames <= 0
        ? 0
        : runtimeAuthoredFramesToFixedTicks(state, authoredFrames);
}

bool spawnRuntimeProjectileTrailerEffect(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile) {
    const int separationTicks = runtimeProjectileTrailerSeparationTicks(
        state,
        projectile
    );
    if (separationTicks <= 0
        || projectile.trailerAnimId.empty()
        || projectile.elapsedTicks <= 0
        || projectile.remainingTicks <= 0
        || projectile.elapsedTicks % separationTicks != 0) {
        return false;
    }

    RuntimeImpactEffect trailer;
    trailer.projectileActorId = projectile.actorId;
    trailer.attackerObjectId = projectile.attackerObjectId;
    trailer.targetObjectId = -1;
    trailer.owner = projectile.owner;
    trailer.cell = projectile.currentCell;
    trailer.level = projectile.currentLevel;
    trailer.hasWorldPosition = true;
    trailer.worldX = projectile.currentX;
    trailer.worldY = projectile.currentY;
    trailer.worldLevel = projectile.currentZ;
    const double muzzleWeight = 1.0 - std::clamp(projectile.progress, 0.0, 1.0);
    trailer.screenOffsetX = static_cast<int>(std::lround(projectile.sourceScreenOffsetX * muzzleWeight));
    trailer.screenOffsetY = static_cast<int>(std::lround(projectile.sourceScreenOffsetY * muzzleWeight));
    trailer.nativeFixedX = static_cast<int>(std::lround(projectile.sourceNativeFixedX * muzzleWeight));
    trailer.nativeFixedY = static_cast<int>(std::lround(projectile.sourceNativeFixedY * muzzleWeight));
    trailer.totalTicks = std::max(8, separationTicks + 1);
    trailer.remainingTicks = trailer.totalTicks;
    trailer.facing = projectile.attackerFacing;
    trailer.lifecycleState = "Trailer";
    trailer.renderIntent = "trailer";
    trailer.weaponId = projectile.weaponId;
    trailer.weaponSlot = projectile.weaponSlot;
    trailer.projectileId = projectile.projectileId;
    trailer.projectileImageId = projectile.projectileImageId;
    trailer.animId = projectile.trailerAnimId;
    trailer.trailerIntentId = projectile.trailerIntentId;
    trailer.bright = projectile.bright;
    initializeRuntimeImpactAnimationTimeline(state, trailer);
    state.impactEffects.push_back(trailer);

    RuntimeParticleSpawnContext particleContext;
    particleContext.sourceObjectId = projectile.attackerObjectId;
    particleContext.owner = projectile.owner;
    particleContext.sourceTag = "trailer:" + projectile.trailerAnimId;
    particleContext.position = {
        projectile.currentX * 256.0,
        projectile.currentY * 256.0,
        projectile.currentZ * 256.0
    };
    particleContext.seed = runtimeStableHashString(
        "RuntimeProjectileTrailer/1"
    );
    runtimeStableHashMix(particleContext.seed, projectile.actorId);
    runtimeStableHashMix(particleContext.seed, projectile.elapsedTicks);
    (void)spawnRuntimeArtAnimationParticles(
        state,
        projectile.trailerAnimId,
        particleContext
    );
    return true;
}

bool syncRuntimeDiskLaserCharge(RuntimeProductionHostState& state, RuntimeProjectileEffect& shot) {
    const auto* owner = findObjectById(state, shot.attackerObjectId);
    const auto* target = findObjectById(state, shot.targetObjectId);
    // Cell force-fire has a saved endpoint, but no live world object. Keep
    // charging against that cell; a lost positive-id unit must still cancel.
    std::optional<WorldObjectInstance> groundTarget;
    if (shot.targetObjectId < 0 && shot.hasTargetPosition) {
        groundTarget = runtimeGroundAttackTargetObject(state, shot.toCell);
        if (groundTarget) target = &*groundTarget;
    }
    const auto* weapon = state.rules.findWeaponType(shot.weaponId);
    if (owner == nullptr || target == nullptr || !owner->alive || !target->alive
        || owner->owner != shot.owner || weapon == nullptr
        || runtimeWeaponDistanceToTarget(state, *owner, *target, *weapon) > weapon->range)
        return false;
    RuntimeWeaponProfile profile;
    profile.weapon = weapon;
    profile.slot = shot.weaponSlot == "elitePrimary"
        ? RuntimeWeaponSlot::ElitePrimary : RuntimeWeaponSlot::Primary;
    auto source = runtimeProjectileSourcePosition(state, *owner, profile,
        runtimeWeaponFiringFacing(state, *owner, *target));
    alignRuntimeWeaponMuzzleToSprite(state, *owner, profile, runtimeWeaponFiringFacing(state, *owner, *target), source);
    shot.sourceX = source.x;
    shot.sourceY = source.y;
    shot.sourceZ = source.z;
    shot.sourceScreenOffsetX = source.screenOffsetX;
    shot.sourceNativeFixedX = source.nativeFixedX;
    shot.sourceScreenOffsetY = source.screenOffsetY;
    shot.sourceNativeFixedY = source.nativeFixedY;
    shot.fromCell = runtimeCellPointForObject(*owner);
    shot.fromLevel = static_cast<int>(std::lround(source.z));
    return true;
}

void spawnRuntimeDiskLaserBeam(RuntimeProductionHostState& state, const RuntimeProjectileEffect& charge) {
    RuntimeProjectileEffect beam = charge;
    const auto& point = runtimeDiskLaserPoints[(charge.attackerFacing / 16) & 15];
    beam.sourceX += point[0] / 256.0;
    beam.sourceY += point[1] / 256.0;
    beam.fromCell = {static_cast<int>(std::lround(beam.sourceX)),
        static_cast<int>(std::lround(beam.sourceY))};
    beam.actorId = nextRuntimeProjectileActorId(state);
    beam.parentActorId = charge.actorId;
    beam.generation = std::min(4, charge.generation + 1);
    beam.renderIntent = "diskLaserBeam";
    beam.targetObjectId = beam.sourceAnchorObjectId = -1;
    beam.projectileId.clear();
    beam.projectileImageId.clear();
    beam.impactAnimId.clear();
    beam.damage = 0;
    beam.laser = true;
    beam.totalTicks = runtimeAuthoredFramesToFixedTicks(state, std::max(1, charge.laserDuration));
    beam.remainingTicks = beam.totalTicks;
    beam.elapsedTicks = 0;
    beam.spawnTick = state.tick;
    beam.impactTick = state.tick + beam.totalTicks;
    beam.lifecycle = RuntimeProjectileLifecycleState::Spawned;
    beam.lifecycleState = runtimeProjectileLifecycleName(beam.lifecycle);
    updateRuntimeProjectileTravelPosition(beam);
    recordRuntimeProjectileReplayEvent(state, beam, "ProjectileSpawned");
    state.projectiles.push_back(std::move(beam));
    if (!charge.reportSoundId.empty()) emitRuntimeSound(state, "weapon:" + charge.reportSoundId, RuntimeSpatialAudioPosition{charge.sourceX, charge.sourceY, charge.sourceZ});
}

double runtimeSonicWaveTail(const RuntimeProjectileEffect& wave) {
    const double dx = wave.targetX - wave.sourceX, dy = wave.targetY - wave.sourceY;
    const double length2 = dx * dx + dy * dy;
    return length2 <= 0.000001 ? 0.0 : std::clamp(
        ((wave.currentX-wave.sourceX)*dx+(wave.currentY-wave.sourceY)*dy)/length2, 0.0, 1.0);
}

const WeaponType* runtimeSonicWaveCurrentWeapon(const RuntimeGameSessionState& state,
    const WorldObjectInstance& source) {
    const auto weaponId = [&](const auto* type) -> std::string {
        if (!type) return {};
        return source.veterancy >= 2 && !type->elitePrimaryWeapon.empty()
            ? type->elitePrimaryWeapon : type->primaryWeapon;
    };
    const std::string id = source.kind == WorldObjectKind::Structure
        ? weaponId(findRuntimeBuildingTypeById(state.rules, source.type))
        : weaponId(findRuntimeUnitType(state.rules, source));
    return findRuntimeWeaponType(state.rules, id);
}

bool advanceRuntimeSonicWave(RuntimeProductionHostState& state, RuntimeProjectileEffect& wave) {
    const int hz = std::max(1, state.config.fixedTickHz);
    const int previousFrame = static_cast<int>(static_cast<long long>(wave.elapsedTicks)*15/hz);
    --wave.remainingTicks;
    wave.elapsedTicks = wave.totalTicks - wave.remainingTicks;
    const int frame = static_cast<int>(static_cast<long long>(wave.elapsedTicks)*15/hz);
    constexpr double step = static_cast<double>(0.05f);
    double tail = runtimeSonicWaveTail(wave);
    const auto* source = runtimeTypeImmuneSource(state, wave.attackerObjectId);
    const auto* target = findObjectById(state, wave.targetObjectId);
    const auto behavior = state.behaviors.find(wave.attackerObjectId);
    const auto cursor = state.combatDetails.fireCursors.find(wave.attackerObjectId);
    bool targeted = behavior != state.behaviors.end()
        ? behavior->second.kind == RuntimeBehaviorKind::Attack
            && behavior->second.targetObjectId == wave.targetObjectId
        : cursor != state.combatDetails.fireCursors.end()
            && cursor->second.lastTargetObjectId == wave.targetObjectId;
    if (!source || (!target && wave.targetObjectId >= 0) || !targeted)
        wave.speedCellsPerTick = 0.0;
    if (source && target) {
        // Retail uses the source object's position, not its FLH, and stops at
        // six diagonal cells: 6 * sqrt(2 * 256^2) leptons (0x762C27).
        const auto* anchor=findObjectById(state,wave.sourceAnchorObjectId);
        const auto& origin=anchor && runtimeCombatValidPassenger(*source,*anchor) ? *anchor : *source;
        const double dx=runtimeObjectRenderX(origin)-runtimeObjectRenderX(*target);
        const double dy=runtimeObjectRenderY(origin)-runtimeObjectRenderY(*target);
        const double dz=(runtimeDrawLevelForObject(origin)-runtimeDrawLevelForObject(*target))/std::sqrt(6.0);
        const int distanceLeptons=static_cast<int>(std::sqrt(dx*dx+dy*dy+dz*dz)*256.0);
        if(distanceLeptons > 6.0*std::sqrt(2.0)*256.0) wave.speedCellsPerTick=0.0;
    }
    if (wave.speedCellsPerTick > 0.0 && source && target) {
        const auto* anchor=findObjectById(state,wave.sourceAnchorObjectId);
        const auto pose=runtimeCombatFiringPose(*source,
            anchor && runtimeCombatValidPassenger(*source,*anchor) ? anchor : nullptr);
        const auto profile = runtimeWeaponProfileForObject(state, pose, target);
        auto muzzle = runtimeProjectileSourcePosition(state, pose, profile,
            runtimeWeaponFiringFacing(state, pose, *target));
        alignRuntimeWeaponMuzzleToSprite(state, pose, profile, runtimeWeaponFiringFacing(state, pose, *target), muzzle);
        const auto aim = runtimeProjectileTargetPosition(state, *target);
        wave.sourceX=muzzle.x; wave.sourceY=muzzle.y; wave.sourceZ=muzzle.z;
        wave.sourceScreenOffsetX=muzzle.screenOffsetX; wave.sourceScreenOffsetY=muzzle.screenOffsetY;
        wave.sourceNativeFixedX=muzzle.nativeFixedX; wave.sourceNativeFixedY=muzzle.nativeFixedY;
        wave.targetX=aim.x; wave.targetY=aim.y; wave.targetZ=aim.level+runtimeWeaponHeightInLevels(50);
        wave.fromCell=runtimeCellPointForObject(pose); wave.toCell=aim.cell;
    }
    bool expired = wave.remainingTicks <= 0;
    for (int logical = previousFrame; logical < frame && !expired; ++logical) {
        // Retail latches retraction at 0x762B22. A positive saved tail preserves
        // that latch even after the advancing front reaches its destination.
        const bool retract = tail > 0.0 || wave.progress-tail > 0.1 || wave.speedCellsPerTick <= 0.0;
        if (wave.speedCellsPerTick > 0.0) wave.progress = std::min(1.0, wave.progress+step);
        if (retract) tail += step;
        if (tail >= wave.progress) { expired=true; break; }
        source=runtimeTypeImmuneSource(state, wave.attackerObjectId);
        const auto* weapon = source ? runtimeSonicWaveCurrentWeapon(state, *source) : nullptr;
        if (!source || !weapon || weapon->ambientDamage == 0) continue;
        const auto cells=runtimeSonicWaveDamageCells(state,wave,tail);
        for (int key : cells) {
            const auto* cell=state.terrainByCell.at(key);
            RuntimeProjectileEffect pulse=wave;
            pulse.actorId=nextRuntimeProjectileActorId(state);
            pulse.renderIntent="sonicAmbientPulse";
            pulse.targetObjectId=-1; pulse.toCell={cell->x,cell->y};
            pulse.targetX=cell->x; pulse.targetY=cell->y; pulse.targetZ=wave.sourceZ;
            pulse.toLevel=static_cast<int>(std::lround(wave.sourceZ));
            pulse.hasTargetPosition=true; pulse.cellSpread=0.0;
            pulse.damage=weapon->ambientDamage; pulse.veteranDamageMultiplier=1.0;
            pulse.weaponId=weapon->id; pulse.warheadId=weapon->warhead;
            pulse.particleDamagePulse=true; pulse.suppressWarheadParticleSpawn=true;
            // WaveClass calls ReceiveDamage directly, without the blast's
            // AffectsAllies filter. TypeImmune is checked at the damage gate.
            pulse.affectsAllies=pulse.affectsEnemies=true;
            pulse.owner=source->owner;
            (void)applyRuntimeProjectileImpact(state,pulse);
            source=runtimeTypeImmuneSource(state,wave.attackerObjectId);
            if (!source) break;
        }
    }
    wave.currentX=wave.sourceX+(wave.targetX-wave.sourceX)*tail;
    wave.currentY=wave.sourceY+(wave.targetY-wave.sourceY)*tail;
    wave.currentZ=wave.sourceZ+(wave.targetZ-wave.sourceZ)*tail;
    wave.currentCell={static_cast<int>(std::floor(wave.currentX+0.5)),
        static_cast<int>(std::floor(wave.currentY+0.5))};
    wave.currentLevel=static_cast<int>(std::lround(wave.currentZ));
    if (expired) {
        wave.totalTicks=std::max(1,wave.elapsedTicks); wave.remainingTicks=0;
        wave.elapsedTicks=wave.totalTicks; wave.impactTick=wave.spawnTick+wave.totalTicks;
    } else {
        wave.lifecycle=RuntimeProjectileLifecycleState::Traveling;
        wave.lifecycleState=runtimeProjectileLifecycleName(wave.lifecycle);
    }
    return !expired;
}

void initializeRuntimeSuperWeaponAnimation(RuntimeProductionHostState& state, RuntimeImpactEffect& animation) {
    initializeRuntimeImpactAnimationTimeline(state, animation, true);
    if (animation.animationSequenceSteps <= 0) return;
    const auto* art = state.art.findArtType(animation.animId);
    const int hz = std::max(1, state.config.fixedTickHz);
    // artmd Rate is frames per game minute (900 original ticks), not ticks
    // in this host's 30/60 Hz simulation.
    animation.animationFrameDelayTicks = art && art->rate > 0
        ? std::max(1, std::max(1, 900 / art->rate) * hz / 15)
        : std::max(1, hz / 15); // AnimTypeClass default delay is one native frame.
    animation.totalTicks = animation.remainingTicks =
        animation.animationSequenceSteps * animation.animationFrameDelayTicks;
}

int runtimeSpecialDetailAnimationDurationTicks(RuntimeProductionHostState& state,
    const std::string& animationId) {
    if (animationId.empty() || !state.artLoaded) return 0;
    RuntimeImpactEffect animation;
    animation.animId = animationId;
    initializeRuntimeSuperWeaponAnimation(state, animation);
    return animation.animationSequenceSteps > 0
        ? animation.animationSequenceSteps * animation.animationFrameDelayTicks : 0;
}

int runtimeSpecialDetailAnimationMidpointTicks(RuntimeProductionHostState& state,
    const std::string& animationId) {
    RuntimeImpactEffect animation; animation.animId=animationId;
    initializeRuntimeSuperWeaponAnimation(state,animation);
    return animation.animationSequenceSteps > 0
        ? (animation.animationSequenceSteps/2+1)*animation.animationFrameDelayTicks : 0;
}

bool runtimeSupportPowerCanPlace(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int x, int y) {
    auto grounded = object;
    grounded.airborne = false;
    grounded.authoredAircraft = false;
    const auto occupied = buildRuntimeOccupancyLookup(state, object.instanceId);
    return runtimeCellIsPassable(state, state.terrainByCell, occupied, {x, y}, grounded);
}

bool runtimeSupportPowerFatalWaterDestination(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int x, int y) {
    const RuntimeCellPoint cell{x, y};
    if (!runtimeVehicleCannotSurviveOpenWaterAtCell(state, object, cell)
        || runtimeFoundationBlocksMovement(state, object, cell, -1, false,
            RuntimeGatePassageMode::Commit)) return false;
    // Test hard occupancy while ignoring the water-domain failure itself.
    // Surface ships, full building foundations and terrain objects still block.
    const auto occupied = buildRuntimeOccupancyLookup(state, object.instanceId);
    return !occupied.contains(runtimeCellKey(x, y));
}

int runtimeSupportPowerParatrooperSubcell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int x, int y, double desiredX, double desiredY) {
    auto grounded = object;
    grounded.airborne = grounded.authoredAircraft = false;
    const auto available = state.subcells.availableSubCells(state.world.getObjects(),
        state.world.getTerrainObjects(), runtimeSubcellLocationForCell(state, {x, y}),
        {object.instanceId}, runtimeGateSubcellContext(state, &grounded));
    int selected = -1;
    double nearest = std::numeric_limits<double>::infinity();
    for (int slot : available) {
        grounded.subCell = slot;
        const auto offset = worldObjectSubCellOffset(grounded);
        const double distance = std::pow(x + offset.x - desiredX, 2)
            + std::pow(y + offset.y - desiredY, 2);
        if (distance < nearest) { nearest = distance; selected = slot; }
    }
    return selected;
}

void consumeRuntimeSuperWeaponRelocationEvent(RuntimeGameSessionState& state,
    WorldObjectInstance& relocated, const SuperWeaponObjectLifecycleEvent& event) {
    auto* object = &relocated;
            state.behaviors.erase(event.objectId);
            object->cell = runtimeCellKey(object->x, object->y);
            object->level = terrainLevelAtRuntimeCell(state, object->x, object->y);
            object->onBridge = runtimeCellIsBridge(state, object->x, object->y);
            object->hasRenderPosition = false;
            object->renderMoveActive = false;
            const auto* sourceRule = state.rules.findSuperWeaponType(event.superWeaponId);
            if (sourceRule && toLowerAsciiLocal(sourceRule->type + " " + sourceRule->action)
                    .find("chrono") != std::string::npos) {
                state.specialMechanisms.beginChronosphereRecovery(
                    *object, event.tick, state.config.fixedTickHz);
            }
}

void invalidateRuntimeSuperWeaponWorldQueries(RuntimeProductionHostState& state) {
    state.navigationRefreshPending = true;
    state.projectedObjectsCacheValid = false;
    state.repaintRequested = true;
}

bool flushRuntimeSuperWeaponHostEvents(RuntimeProductionHostState& state) {
    const auto before=state.impactEffects.size();
    std::erase_if(state.impactEffects,[&](const auto& effect) {
        if(effect.lifecycleState=="NuclearTargetWarning") {
            const auto& snapshots=state.superWeapons.renderSnapshots();
            return std::none_of(snapshots.begin(),snapshots.end(),[&](const auto& snapshot) {
                return snapshot.kind=="superWeaponProjectile" && snapshot.owner==effect.owner
                    && snapshot.superWeaponId==effect.weaponId
                    && snapshot.cell.x==effect.cell.x && snapshot.cell.y==effect.cell.y;
            });
        }
        if(effect.lifecycleState!="ChronosphereSelection") return false;
        const auto* sidebar=state.superWeapons.findSidebarSnapshot(effect.owner,effect.weaponId);
        return !sidebar || sidebar->state!=SuperWeaponRuntimeState::AwaitingSecondTarget
            || sidebar->firstTargetCell.x!=effect.cell.x || sidebar->firstTargetCell.y!=effect.cell.y;
    });
    for(auto& effect:state.impactEffects) if((effect.lifecycleState=="ChronosphereSelection"
        || effect.lifecycleState=="NuclearTargetWarning")
        && effect.remainingTicks<=effect.animationFrameDelayTicks*2)
        effect.remainingTicks=effect.totalTicks-effect.animationLoopStartFrame*effect.animationFrameDelayTicks;
    if (!state.remoteHumanHouseScopeActive) advanceRuntimeChronosphereSelectionAudio(state, runtimeWorldAudioClockNow(state));
    if (state.pendingSuperWeaponObjectEvents.empty()
        && state.pendingSuperWeaponImpacts.empty()
        && state.pendingSuperWeaponVisuals.empty()) return before!=state.impactEffects.size();
    auto objects = std::move(state.pendingSuperWeaponObjectEvents);
    auto impacts = std::move(state.pendingSuperWeaponImpacts);
    auto visuals = std::move(state.pendingSuperWeaponVisuals);
    state.pendingSuperWeaponObjectEvents.clear();
    state.pendingSuperWeaponImpacts.clear();
    state.pendingSuperWeaponVisuals.clear();

    std::vector<RuntimeOwnerTransitionRoot> owners;
    bool relocatedGroundObject = false;
    for (const auto& event : objects) {
        auto* object = findMutableObjectById(state, event.objectId);
        if (!object || !object->alive) continue;
        if (event.kind == SuperWeaponObjectLifecycleKind::OwnerChanged) {
            // The independent engine has already assigned the new owner.
            // Restore the expected source, then commit through the same
            // relationship transaction used by ordinary mind control.
            object->owner = event.oldOwner;
            owners.push_back({event.objectId, event.oldOwner, event.newOwner});
        } else if (event.kind == SuperWeaponObjectLifecycleKind::Relocated) {
            consumeRuntimeSuperWeaponRelocationEvent(state, *object, event);
            relocatedGroundObject = relocatedGroundObject || !object->airborne;
        } else if (event.kind == SuperWeaponObjectLifecycleKind::Damaged) {
            state.cloakObservedHealthByObject[event.objectId] = object->health;
            state.powerRefreshPending = state.powerRefreshPending || object->kind == WorldObjectKind::Structure;
        }
    }
    if (!owners.empty()) {
        const auto result = applyRuntimeOwnerTransitionBatch(state, owners,
            RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits, "psychicDominatorOwnerChanged");
        if (!result.applied) state.simulationStatus = "psychic dominator ownership failed: " + result.error;
        else for (const auto& event : objects) {
            if (event.kind != SuperWeaponObjectLifecycleKind::OwnerChanged) continue;
            auto* target = findMutableObjectById(state, event.objectId);
            if (!target || target->owner != event.newOwner) continue;
            state.specialMechanisms.establishDominatorControl(event.objectId,
                event.newOwner, event.oldOwner, event.tick);
            target->mission = "MindControlledPermanent";
        }
    }
    // Landing/teleportation can occur with no active movement behaviors.
    // Commit ground slots here; the idle simulation path may return before
    // the ordinary movement reconciliation, and saves require valid slots.
    if (relocatedGroundObject) (void)reconcileRuntimeSubcells(state, true);

    auto animation = [&](const std::string& id, const SuperWeaponCell& cell,
        const std::string& warhead, int damage, int radius, int sourceId, const std::string& owner,
        double heightLeptons = 0.0, bool managedAnimationSound = false,
        const std::array<double, 3>* worldPosition = nullptr,
        const std::array<int, 4>* canvasOffsets = nullptr) {
        if (id.empty() || !state.artLoaded || !state.art.findArtType(id)) return;
        RuntimeProjectileEffect source;
        source.attackerObjectId = sourceId;
        source.owner = owner;
        source.warheadId = warhead;
        RuntimeProjectileTargetPosition precise;
        if (worldPosition) {
            precise.cell = {cell.x, cell.y};
            precise.x = (*worldPosition)[0]; precise.y = (*worldPosition)[1];
            precise.level = static_cast<int>(std::lround((*worldPosition)[2]));
        }
        spawnRuntimeImpact(state, {cell.x, cell.y}, terrainLevelAtRuntimeCell(state, cell.x, cell.y),
            false, id, warhead, damage, radius, false, "", &source,
            worldPosition ? &precise : nullptr, nullptr, managedAnimationSound);
        auto& effect=state.impactEffects.back();
        effect.renderIntent="superWeaponEffect";
        if (worldPosition) effect.worldLevel = (*worldPosition)[2];
        if (canvasOffsets) {
            effect.screenOffsetX = (*canvasOffsets)[0];
            effect.screenOffsetY = (*canvasOffsets)[1];
            effect.nativeFixedX = (*canvasOffsets)[2];
            effect.nativeFixedY = (*canvasOffsets)[3];
        }
        // Ordinary hit flashes may use a short default lifetime; these
        // authored superweapon sequences must play their complete SHP.
        initializeRuntimeSuperWeaponAnimation(state,effect);
        if (heightLeptons != 0.0) {
            effect.hasWorldPosition = true;
            effect.worldX = cell.x; effect.worldY = cell.y;
            effect.worldLevel = effect.level + heightLeptons * 2.449489742783178 / 256.0;
        }
    };
    for (const auto& impact : impacts) {
        std::string id = impact.animationId;
        if (id.empty()) {
            if (const auto* warhead = state.rules.findWarheadType(impact.warheadId)) id = warhead->animList;
        }
        const auto comma = id.find(',');
        if (comma != std::string::npos) id.resize(comma);
        const auto* impactRule = state.rules.findSuperWeaponType(impact.superWeaponId);
        if (impact.kind == "LightningCloud") {
            RuntimeImpactEffect bolt; bolt.animId = "WCLBOLT1";
            initializeRuntimeSuperWeaponAnimation(state, bolt);
            const double height = bolt.animationMidpointHeight > 0
                ? (bolt.animationMidpointHeight / 2) * 256.0 / (15.0 * 2.449489742783178) : 1200.0;
            animation(id, impact.cell, "", 0, 0, impact.sourceObjectId, impact.sourceOwner, height);
            continue;
        }
        if (impact.kind != "PsychicDominatorImpact" || !impactRule
            || trimAsciiLocal(id) != impactRule->firstAnim) {
            animation(trimAsciiLocal(id), impact.cell, impact.warheadId, impact.damage,
                impact.radius, impact.sourceObjectId, impact.sourceOwner);
        }
        if (impact.kind == "LightningBolt") {
            // Retail 0x53A300 creates the bolt at the ground cell. Its SHP
            // already extends upward to the cloud; do not lift it twice.
            animation(impactRule ? impactRule->boltExplosionAnim : "", impact.cell, "", 0, 0,
                impact.sourceObjectId, impact.sourceOwner);
        }
        if (impact.kind == "PsychicDominatorImpact") {
            const auto* rule = state.rules.findSuperWeaponType(impact.superWeaponId);
            if (rule) animation(rule->secondAnim, impact.cell, "", 0, 0, impact.sourceObjectId, impact.sourceOwner);
        }
    }
    for (const auto& visual : visuals) {
        const auto& event = visual.event;
        const auto* rule = state.rules.findSuperWeaponType(event.superWeaponId);
        if (!rule) continue;
        auto cell = event.targetCell;
        if (event.kind != "GeneticMutationApplied")
            if (const auto* object = findObjectById(state, event.objectId)) cell = {object->x, object->y};
        if (event.kind == "SuperWeaponFirstTargetAccepted") {
            const auto* sidebar=state.superWeapons.findSidebarSnapshot(event.owner,event.superWeaponId);
            if(sidebar && sidebar->state==SuperWeaponRuntimeState::AwaitingSecondTarget && !rule->placementAnim.empty()) {
                animation(rule->placementAnim,event.targetCell,"",0,0,-1,event.owner,5.0,true);
                if(!state.impactEffects.empty() && state.impactEffects.back().animId==rule->placementAnim) {
                    auto& effect=state.impactEffects.back(); effect.lifecycleState="ChronosphereSelection";
                    effect.weaponId=event.superWeaponId; effect.animationLoopCount=1000;
                    effect.animationSequenceSteps=effect.animationLoopStartFrame
                        +(effect.animationLoopEndFrame-effect.animationLoopStartFrame)*1000;
                    effect.remainingTicks=effect.totalTicks=effect.animationSequenceSteps*effect.animationFrameDelayTicks;
                }
                if (!state.impactEffects.empty() && state.impactEffects.back().animId == rule->placementAnim)
                    startRuntimeChronosphereSelectionAudio(state, state.impactEffects.back(), runtimeWorldAudioClockNow(state));
            }
        } else if (event.kind == "PsychicDominatorStarted") {
            animation(rule->firstAnim, event.targetCell, "", 0, 0, -1, event.owner, 750.0);
        } else if (event.kind == "SuperWeaponLaunched"
            && toLowerAsciiLocal(rule->type + " " + rule->action).find("ironcurtain") != std::string::npos) {
            // The invocation belongs to the selected ground cell, including
            // an empty area. Object protection only drives the persistent tint.
            animation(rule->invokeAnim, event.targetCell, "", 0, 0, -1, event.owner);
        } else if (event.kind == "ForceShieldProtected") {
            animation(rule->invokeAnim, cell, "", 0, 0, -1, event.owner);
        } else if (event.kind == "GeneticMutationApplied") {
            animation(rule->impactAnim, cell, "", 0, 0, -1, event.owner, 0.0, false,
                visual.worldPosition ? &*visual.worldPosition : nullptr,
                &visual.canvasOffsets);
        } else if (event.kind == "ChronoWarpIn") {
            animation(rule->impactAnim, cell, "", 0, 0, -1, event.owner);
        } else if (event.kind == "ChronoSphereApplied") {
            animation(rule->firstAnim, event.targetCell, "", 0, 0, -1, event.owner);
            animation(rule->secondAnim, event.targetCell2, "", 0, 0, -1, event.owner);
        } else if (event.kind == "SuperWeaponLaunched"
            && toLowerAsciiLocal(rule->type).find("genetic") != std::string::npos) {
            animation(rule->invokeAnim, event.targetCell, "", 0, 0, -1, event.owner, 5.0);
        } else if (event.kind == "SuperWeaponLaunched" && rule->nuclearAltitudeLeptons>0) {
            animation("PSIWARN",event.targetCell,"",0,0,-1,event.owner);
            if(!state.impactEffects.empty() && state.impactEffects.back().animId=="PSIWARN") {
                auto& effect=state.impactEffects.back(); effect.lifecycleState="NuclearTargetWarning";
                effect.weaponId=event.superWeaponId; effect.animationLoopCount=1000;
                effect.animationSequenceSteps=effect.animationLoopStartFrame
                    +(effect.animationLoopEndFrame-effect.animationLoopStartFrame)*1000;
                effect.remainingTicks=effect.totalTicks=effect.animationSequenceSteps*effect.animationFrameDelayTicks;
            }
        }
    }
    invalidateRuntimeSuperWeaponWorldQueries(state);
    return true;
}

bool runtimeObjectIgnoresDamage(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (runtimeCarrierAircraftHasDeckProtection(state, object)) return true;
    if (object.kind != WorldObjectKind::Structure) {
        const auto* unit = findRuntimeUnitType(state.rules, object);
        return unit && unit->immune;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object.type);
    return building != nullptr
        && (building->immune || building->bridgeRepairHut);
}

}
