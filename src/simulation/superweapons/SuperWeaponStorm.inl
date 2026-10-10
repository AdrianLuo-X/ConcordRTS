void SuperWeaponSystem::advanceStorm(Instance& instance,
    const SuperWeaponWorldContext& context) {
    const auto* rule = findRule(instance.definitionId);
    if (!rule) return;
    if(instance.nextImpactTick<0) {
        // v1-v6 stored the next scatter tick here and already delivered the
        // saved clouds' damage. Preserve their original end time on migration.
        instance.nextImpactTick=instance.activeUntilTick;
        instance.nextEffectTick=instance.stateStartTick
            + std::max(1,convertRuleFrames(rule->defermentFrames,context.fixedTickHz));
        instance.effectApplied=context.tick>=instance.nextEffectTick;
    }
    const auto bolts = splitValues(rule->boltAnim);
    const auto clouds = splitValues(rule->cloudAnim);
    const auto* warhead = context.rules ? context.rules->findWarheadType(rule->payloadWarhead) : nullptr;
    const int radius = warhead ? std::max(0, int(std::ceil(warhead->cellSpread))) : 0;
    // A cloud appears first. Its bolt and damage arrive halfway through the
    // authored sequence, including when that pending strike was saved.
    for (auto& cloud : instance.stormClouds) {
        if (cloud.impactTick < 0 || context.tick < cloud.impactTick) continue;
        cloud.impactTick = -1;
        const std::string bolt = bolts.empty() ? "" : bolts[cloud.boltIndex % bolts.size()];
        emitImpact(context, instance, "LightningBolt", cloud.cell, radius,
            rule->damage, rule->payloadWarhead, bolt);
        applyRadialDamage(instance, context, cloud.cell, radius, rule->damage,
            rule->payloadWarhead, "LightningBoltDamage");
        SuperWeaponTarget target{cloud.cell, -1};
        emit(context, instance, "LightningBolt", cloud.direct ? "direct" : "scatter",
            &target, nullptr, -1, -1, cloud.direct ? 1 : 0, rule->specialSound, "", bolt);
    }
    std::erase_if(instance.stormClouds, [&](const StormCloud& cloud) {
        return context.tick >= cloud.untilTick;
    });
    const int start = instance.nextEffectTick;
    const int stop = instance.nextImpactTick >= 0 ? instance.nextImpactTick
        : start + std::max(1, convertRuleFrames(rule->durationFrames, context.fixedTickHz));
    if (context.tick < start || context.tick > stop) return;
    if (!instance.effectApplied) {
        instance.effectApplied = true;
        emit(context, instance, "LightningStormManifested", "clouds forming",
            &instance.firstTarget, nullptr, -1, -1, 0, rule->startSound);
    }
    // Retail 0x53A944 uses the global 15 Hz frame, with independent clocks.
    const auto frame = std::int64_t(context.tick) * 15 / std::max(1, context.fixedTickHz);
    const auto previousFrame = std::int64_t(context.tick-1) * 15 / std::max(1, context.fixedTickHz);
    const auto due = [&](int frames) {
        return frames > 0 && frame != previousFrame && frame % frames == 0;
    };
    const auto random = [&]() {
        return instance.rngState = instance.rngState * 1664525U + 1013904223U;
    };
    const auto create = [&](bool direct) {
        if (clouds.empty()) return;
        SuperWeaponCell cell = instance.firstTarget.cell;
        if (!direct) {
            // Retail 0x53A980..0x53AA92: +/- CellSpread/2 on both axes,
            // inclusive endpoints, a square, and at most three attempts.
            const int half = std::clamp(rule->cellSpread, 0, 32766) / 2;
            const int diameter = 2 * half + 1;
            bool placed = false;
            for (int attempt = 0; attempt < 3; ++attempt) {
                const int dx = int(random() % unsigned(diameter)) - half;
                const int dy = int(random() % unsigned(diameter)) - half;
                cell = {instance.firstTarget.cell.x+dx, instance.firstTarget.cell.y+dy};
                if (context.cellLegal && !context.cellLegal(cell.x, cell.y, SuperWeaponTargetMode::Area)) continue;
                if (std::any_of(instance.stormClouds.begin(), instance.stormClouds.end(), [&](const auto& other) {
                        return std::abs(cell.x-other.cell.x)+std::abs(cell.y-other.cell.y) < rule->separation;
                    })) continue;
                placed = true; break;
            }
            if (!placed) return;
        }
        const auto seed = random();
        const std::string& id = clouds[(seed >> 16) % clouds.size()];
        const int authored = context.animationDurationTicks ? context.animationDurationTicks(id) : 0;
        const int duration = authored > 0 ? authored : std::max(2, convertRuleFrames(30, context.fixedTickHz));
        StormCloud cloud;
        cloud.cell = cell;
        cloud.untilTick = context.tick + duration;
        const int midpoint=context.animationMidpointTicks ? context.animationMidpointTicks(id) : 0;
        cloud.impactTick = context.tick + std::clamp(midpoint>0 ? midpoint : duration/2+1,1,duration-1);
        cloud.boltIndex = bolts.empty() ? 0 : int(seed % bolts.size());
        cloud.direct = direct;
        instance.stormClouds.push_back(cloud);
        emitImpact(context, instance, "LightningCloud", cell, 0, 0, "", id);
    };
    // Independent clocks: a direct cloud never displaces a scatter cloud,
    // and direct strikes are exempt from LightningSeparation.
    if (due(rule->hitDelayFrames)) create(true);
    if (due(rule->scatterDelayFrames)) create(false);
}

bool SuperWeaponSystem::isEnemyStormActive(const std::string& owner, int tick,
    const std::function<bool(const std::string&, const std::string&)>& areAllied) const {
    for (const auto& instance : instances_) {
        const auto* rule = findRule(instance.definitionId);
        if (!rule || !isLightningLifecycle(*rule)
            || instance.state != SuperWeaponRuntimeState::Active
            || tick >= (instance.nextImpactTick >= 0 ? instance.nextImpactTick : instance.activeUntilTick)
            || lowerAscii(owner) == lowerAscii(instance.owner)
            || (areAllied && areAllied(owner, instance.owner))) continue;
        // Radar outage begins when the storm manifests, after the warning.
        if (tick >= instance.nextEffectTick) return true;
    }
    return false;
}
