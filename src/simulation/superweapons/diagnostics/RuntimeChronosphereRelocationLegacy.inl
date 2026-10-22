bool s15LegacyFlushRuntimeSuperWeaponHostEvents(RuntimeWindowState& state) {
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
    if (!state.remoteHumanHouseScopeActive) advanceRuntimeChronosphereSelectionAudio(state, GetTickCount64());
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
                    startRuntimeChronosphereSelectionAudio(state, state.impactEffects.back(), GetTickCount64());
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
    state.navigationRefreshPending = true;
    state.projectedObjectsCacheValid = false;
    state.repaintRequested = true;
    return true;
}
