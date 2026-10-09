// Exercises the real lethal-hit/exit/effect paths with the mounted game art.
bool runRuntimeInfantryDeathSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& reason) { error = reason; return false; };
    IniFile authored;
    authored.loadFromString(
        "[ALIAS]\nImage=GI\nSequence=DeathTest\nFacings=8\n"
        "[DeathTest]\nDie1=20,4,4\nDie2=52,3,0\nDie3=-1,4,0\n"
        "WetDie1=60,5,0\nDie5=70,0,0\n");
    ArtDatabase fixtureArt;
    fixtureArt.loadFromIni(authored);
    UnitType fixtureUnit;
    fixtureUnit.id = "SOLDIER";
    fixtureUnit.image = "ALIAS";
    const auto northEast = runtimeInfantryDeathSequence(fixtureArt, fixtureUnit, 1, 32, false);
    const auto explosion = runtimeInfantryDeathSequence(fixtureArt, fixtureUnit, 2, 0, false);
    const auto wet = runtimeInfantryDeathSequence(fixtureArt, fixtureUnit, 1, 0, true);
    fixtureUnit.notHuman = true;
    const auto invalid = runtimeInfantryDeathSequence(fixtureArt, fixtureUnit, 3, 0, false);
    if (!northEast || northEast->startFrame != 48 || northEast->frameCount != 4
        || !explosion || explosion->startFrame != 52
        || !wet || wet->startFrame != 60 || !invalid || invalid->startFrame != 20) {
        return fail("authored death sequences, facing, water or invalid-sequence fallback");
    }
    IniFile rulesIni;
    rulesIni.loadFromString("[General]\nFlamingInfantry=MODFIRE\nInfantryVirus=MODVIRUS\n"
        "InfantryMutate=MODMUTATE\nInfantryBrute=MODSMASH\n");
    RulesDatabase fixtureRules;
    fixtureRules.loadFromIni(rulesIni);
    if (fixtureRules.getInfantryDeathAnim(4) != "MODFIRE"
        || fixtureRules.getInfantryDeathAnim(8) != "MODVIRUS"
        || fixtureRules.getInfantryDeathAnim(5) != "ELECTRO"
        || fixtureRules.getInfantryDeathAnim(9) != "MODMUTATE"
        || fixtureRules.getInfantryDeathAnim(10) != "MODSMASH"
        || !fixtureRules.getInfantryDeathAnim(-1).empty()) {
        return fail("custom General infantry death animations");
    }

    int checked = 0;
    for (const auto& [type, warheadId] : std::vector<std::pair<std::string, std::string>>{
            {"E1", "SA"}, {"E1", "HE"}, {"E2", "SA"}, {"ENGINEER", "SA"},
            {"DOG", "SA"}, {"E1", "Fire"}, {"E1", "Electric"}, {"E1", "Virus"},
            {"E1", "crush"}, {"HTNK", "crush"}, {"E1", "consumed"},
            {"E1", "contained"}, {"E1", "temporal"}, {"E1", "Battering"}}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        RuntimeWindowState& probe = *storage;
        probe.config = live.config;
        probe.rules = live.rules;
        probe.art = live.art;
        probe.rulesLoaded = true;
        probe.artLoaded = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.tick = 17;
        WorldObjectInstance object;
        object.instanceId = 20;
        object.kind = type == "HTNK" ? WorldObjectKind::Unit : WorldObjectKind::Infantry;
        object.type = type;
        object.owner = "Russians";
        object.health = 10;
        object.x = 8;
        object.y = 8;
        object.cell = runtimeCellKey(8, 8);
        object.subCell = 2;
        object.facing = 96;
        object.hasRenderPosition = true;
        object.renderX = 8.125;
        object.renderY = 7.875;
        object.renderMoveActive = true;
        object.renderLevel = 2.5;
        object.onBridge = true;
        object.renderOnBridge = true;
        const bool hidden = warheadId == "contained";
        if (hidden) {
            object.alive = false;
            object.mission = "Transported";
        }
        probe.world.accessObjects().push_back(object);
        probe.selectedObjectIds = {20};
        const bool consumed = warheadId == "consumed";
        const bool temporal = warheadId == "temporal";
        if (warheadId == "crush" || consumed || hidden || temporal) {
            RuntimeObjectExitRequest request;
            request.objectId = 20;
            request.tick = probe.tick;
            request.kind = consumed ? RuntimeObjectExitKind::Consumed
                : temporal ? RuntimeObjectExitKind::TemporalDestroyed
                           : RuntimeObjectExitKind::Destroyed;
            if (!finalizeRuntimeObjectExit(probe, request).accepted) {
                return fail(type + "/" + warheadId + " exit rejected");
            }
        } else {
            RuntimeProjectileEffect projectile;
            projectile.actorId = 41;
            projectile.targetObjectId = 20;
            projectile.owner = "Americans";
            projectile.toCell = {8, 8};
            projectile.currentCell = projectile.toCell;
            projectile.damage = 10000;
            projectile.warheadId = warheadId;
            projectile.impactAnimId = "S_CLSN22";
            projectile.ag = true;
            projectile.aa = true;
            projectile.affectsAllies = true;
            projectile.affectsEnemies = true;
            if (!applyRuntimeProjectileImpact(probe, projectile)) {
                return fail(type + "/" + warheadId + " lethal impact rejected");
            }
        }
        if (findObjectById(probe, 20) != nullptr || !probe.selectedObjectIds.empty()) {
            return fail(type + "/" + warheadId + " retained live/selected unit");
        }
        const auto death = std::find_if(probe.impactEffects.begin(), probe.impactEffects.end(),
            [](const RuntimeImpactEffect& effect) { return effect.death; });
        if (consumed || hidden || temporal) {
            if (death != probe.impactEffects.end()) {
                return fail(warheadId + " incorrectly created a visible corpse");
            }
            ++checked;
            continue;
        }
        if (death == probe.impactEffects.end()
            || std::count_if(probe.impactEffects.begin(), probe.impactEffects.end(),
                [](const RuntimeImpactEffect& effect) { return effect.death; }) != 1) {
            return fail(type + "/" + warheadId + " missing/duplicate death animation");
        }
        RuntimeObjectExitRequest duplicate;
        duplicate.objectId = 20;
        duplicate.tick = probe.tick;
        const std::size_t effectCount = probe.impactEffects.size();
        if (!finalizeRuntimeObjectExit(probe, duplicate).duplicate
            || probe.impactEffects.size() != effectCount) {
            return fail("duplicate exit replayed the death animation");
        }
        const bool sequence = death->renderIntent == "infantryDeath";
        if (warheadId == "Battering" && (death->animId != "BRUTDIE"
            || std::any_of(probe.impactEffects.begin(), probe.impactEffects.end(),
                [](const auto& effect) { return effect.animId == "GENDEATH"; })))
            return fail("Brute victim played mutation instead of the authored melee destruction");
        const bool expectsSequence = type != "HTNK"
            && (warheadId == "SA" || warheadId == "HE" || warheadId == "crush");
        const WorldObjectCellOffset offset = worldObjectSubCellOffset(object);
        if (sequence != expectsSequence || death->owner != object.owner
            || std::abs(death->worldX - object.renderX - offset.x) > 0.0001
            || std::abs(death->worldY - object.renderY - offset.y) > 0.0001
            || (sequence && std::abs(death->worldLevel - object.renderLevel) > 0.0001)) {
            return fail(type + "/" + warheadId + " animation kind, owner or pose");
        }
        if (sequence) {
            const RuntimeImpactEffect saved = *death;
            RuntimeImpactEffect restored;
            if (!parseRuntimeMultiplayerSnapshotImpactRecord(
                    runtimeMultiplayerSnapshotImpactRecord(saved), restored)) {
                return fail("death timeline snapshot could not be restored");
            }
            restored.remainingTicks -= restored.animationFrameDelayTicks * 2;
            *death = restored;
            const auto* actors = runtimeImpactRenderActorsForRender(probe);
            const bool restoredPresentation = actors != nullptr && std::any_of(
                actors->begin(), actors->end(), [&](const RuntimeImpactRenderActor& actor) {
                    return actor.renderIntent == "infantryDeath" && actor.animId == saved.animId
                        && actor.frameIndex == saved.animationStartFrame + 2
                        && actor.owner == saved.owner && actor.x == saved.worldX
                        && actor.y == saved.worldY && actor.level == saved.worldLevel;
                });
            *death = saved;
            if (!restoredPresentation) return fail("snapshot lost or restarted death presentation");
            const int firstFrame = death->frameIndex;
            const int lastFrame = death->animationEndFrame - 1;
            const int duration = death->totalTicks;
            int priorFrame = firstFrame;
            bool advanced = false;
            bool held = false;
            for (int tick = 1; tick <= duration; ++tick) {
                ++probe.tick;
                advanceRuntimeEffects(probe);
                const auto current = std::find_if(probe.impactEffects.begin(), probe.impactEffects.end(),
                    [](const RuntimeImpactEffect& effect) { return effect.renderIntent == "infantryDeath"; });
                if (tick == duration) {
                    if (current != probe.impactEffects.end()) return fail("corpse did not expire");
                    break;
                }
                if (current == probe.impactEffects.end() || current->frameIndex < priorFrame
                    || current->frameIndex > lastFrame) return fail("death sequence looped/skipped lifetime");
                advanced = advanced || current->frameIndex > firstFrame;
                held = held || (priorFrame == lastFrame && current->frameIndex == lastFrame);
                priorFrame = current->frameIndex;
            }
            if (!advanced || !held) return fail("death frames did not advance and hold");
        }
        ++checked;
    }
    std::cout << "Infantry death: cases=" << checked
        << " sequences/facing/water=ok special=ok pose=ok once=ok expiry=ok snapshot=ok\n";
    return true;
}
