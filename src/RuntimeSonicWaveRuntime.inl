double runtimeSonicWaveTail(const RuntimeProjectileEffect& wave) {
    const double dx = wave.targetX - wave.sourceX, dy = wave.targetY - wave.sourceY;
    const double length2 = dx * dx + dy * dy;
    return length2 <= 0.000001 ? 0.0 : std::clamp(
        ((wave.currentX-wave.sourceX)*dx+(wave.currentY-wave.sourceY)*dy)/length2, 0.0, 1.0);
}

const WeaponType* runtimeSonicWaveCurrentWeapon(const RuntimeWindowState& state,
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

bool advanceRuntimeSonicWave(RuntimeWindowState& state, RuntimeProjectileEffect& wave) {
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
