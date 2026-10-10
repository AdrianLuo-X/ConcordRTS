    for (const auto& [id, drain] : state.combatDetails.diskDrains.sources) {
        if (!runtimeDiskDrainRecordActive(state, id, drain)) continue;
        const auto* source = findObjectById(state, id);
        if (!source || source->cloaked) continue;
        const auto& animation = state.rules.getSpecialMechanismRules().drainAnimationType;
        if (animation.empty()) continue;
        RuntimeImpactRenderActor actor;
        actor.actorId = -1;
        actor.attackerObjectId = actor.targetObjectId = id;
        actor.owner = source->owner;
        actor.animId = animation;
        actor.weaponId = drain.weaponId;
        actor.lifecycleState = "DiskDrain";
        actor.renderIntent = "diskDrain";
        actor.x = runtimeObjectRenderX(*source);
        actor.y = runtimeObjectRenderY(*source);
        actor.level = runtimeDrawLevelForObject(*source);
        actor.cellX = source->x; actor.cellY = source->y;
        actor.centerOnAnchor = true;
        actor.totalTicks = std::max(1, state.tick - drain.startedTick + 1);
        actor.remainingTicks = 1;
        actor.frameIndex = -1;
        cache.impactRenderActors.push_back(std::move(actor));
    }
