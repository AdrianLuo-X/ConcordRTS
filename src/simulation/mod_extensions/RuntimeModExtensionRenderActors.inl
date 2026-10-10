    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.cloaked || object.garrisonHostObjectId >= 0
            || object.mission == "InsideTransport" || object.mission == "InTransport") continue;
        int index = 0;
        for (const auto& effect : object.modExtensions.effects) {
            const auto* rule = runtimeAttachedEffectRule(effect, state.rules);
            if (!rule || rule->animation.empty()
                || (rule->temporalHidesAnim && state.specialMechanisms.isTemporalFrozen(object.instanceId))) continue;
            RuntimeImpactRenderActor actor;
            actor.actorId = -1 - index++;
            actor.targetObjectId = object.instanceId;
            actor.owner = object.owner;
            actor.animId = rule->animation;
            actor.lifecycleState = "AttachEffect";
            actor.renderIntent = "attachEffect";
            actor.x = object.hasRenderPosition ? object.renderX : object.x;
            actor.y = object.hasRenderPosition ? object.renderY : object.y;
            actor.level = object.hasRenderPosition ? object.renderLevel : object.level;
            actor.cellX = object.x; actor.cellY = object.y;
            actor.centerOnAnchor = object.kind == WorldObjectKind::Structure;
            actor.totalTicks = effect.elapsedTicks + 1;
            actor.remainingTicks = 1;
            actor.frameIndex = -1;
            cache.impactRenderActors.push_back(std::move(actor));
        }
    }
