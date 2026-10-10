// Retail Techno::ReceiveDamage (0x702823..0x702A04) chooses a Smoke
// system from DamageParticleSystems at ConditionYellow. The global
// DamagedUnitSmokeSystem entry is obsolete and has no stock definition.
const ParticleSystemType* runtimeVehicleDamageSmokeType(
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
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
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
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

int runtimeVehicleDamageSmokeScreenOffset(const RuntimeWindowState& state,
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
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
    const auto* unit = findRuntimeUnitType(state.rules, object);
    if (unit != nullptr && unit->missileSpawn) {
        const auto missile = state.specialMechanisms.spawnedMissiles().find(object.instanceId);
        if (missile != state.specialMechanisms.spawnedMissiles().end())
            return runtimeSpawnedMissileSpriteAttachment(missile->second);
    }
    return {};
}

bool runtimeVehicleDamageSmokePresentation(
    RuntimeWindowState& state, const WorldObjectInstance& object, int screenOffsetY,
    RuntimeParticleAirbornePresentation& presentation, std::string& error,
    std::unique_ptr<RuntimeAircraftAirbornePresentationBatch>* sharedBatch = nullptr
) {
    presentation = {};
    error.clear();
    if (screenOffsetY >= 0) return true;
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
        *sharedBatch = std::make_unique<RuntimeAircraftAirbornePresentationBatch>(
            state.world, context, state.renderCache);
        return (*sharedBatch)->capture(object, presentation, &error);
    }
    return captureRuntimeAircraftAirbornePresentation(state.world, object, context,
        state.renderCache, presentation, &error);
}

void reportRuntimeVehicleSmokePresentationFailure(
    RuntimeWindowState& state, int sourceId, const std::string& error
) {
    const std::string warning = "vehicle damage smoke presentation rejected: source="
        + std::to_string(sourceId) + " " + error;
    if (std::find(state.report.warnings.begin(), state.report.warnings.end(), warning)
        == state.report.warnings.end()) {
        state.report.warnings.push_back(warning);
        std::cerr << warning << '\n';
    }
}

void stopRuntimeVehicleDamageSmokeForSource(RuntimeWindowState& state, int sourceObjectId) {
    if (!state.particlesInitialized) return;
    std::vector<std::uint64_t> stopped;
    for (const auto& system : state.particles.systems()) {
        if (system.emitting && system.sourceObjectId == sourceObjectId
            && system.sourceTag == "vehicle-damage-smoke") stopped.push_back(system.id);
    }
    // stopSystem may remove an empty producer, so do not mutate during traversal.
    for (auto id : stopped) (void)state.particles.stopSystem(id);
}

bool syncRuntimeVehicleDamageSmoke(RuntimeWindowState& state) {
    constexpr std::string_view tag = "vehicle-damage-smoke";
    std::map<int, std::uint64_t> active;
    std::vector<std::uint64_t> stopped;
    // Lazily build one world-coordinate structure index for all ordinary air
    // sources in this update. Ground units and generated missiles need none.
    std::unique_ptr<RuntimeAircraftAirbornePresentationBatch> presentationBatch;
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
