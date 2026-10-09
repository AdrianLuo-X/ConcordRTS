RuntimeVehicleBodyPose runtimeVehicleBodyPoseReferenceForContract(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context
) {
    RuntimeVehicleBodyPose pose;
    if (context.rules == nullptr || object.kind != WorldObjectKind::Unit
        || !object.alive || object.health <= 0 || object.dockedToObjectId >= 0
        || object.submerged || object.cloaked) return pose;
    const auto* unit = context.rules->findUnitType("Vehicle", object.type);
    if (unit == nullptr || unit->naval || unit->jumpJet || unit->balloonHover) return pose;
    const double hz = std::max(1, context.fixedTickHz);
    if (context.specialMechanismSnapshots != nullptr) {
        for (const auto& snapshot : *context.specialMechanismSnapshots) {
            if (snapshot.kind != "parasite" || snapshot.targetObjectId != object.instanceId
                || snapshot.phase == "organic" || snapshot.intensity <= 0) continue;
            const auto* warhead = context.rules->findWarheadType(snapshot.resourceId);
            if (warhead != nullptr && !warhead->rocker) continue;
            const int biteAge = runtimeVehicleParasiteBiteAgeTicks(snapshot.progress,
                snapshot.requiredProgress, snapshot.expireTick, context.animationTick);
            pose = runtimeVehicleParasiteMotion(biteAge / hz, unit->weight);
            break;
        }
    }
    if (context.impactActors != nullptr) {
        const double x = object.hasRenderPosition ? object.renderX : object.x;
        const double y = object.hasRenderPosition ? object.renderY : object.y;
        const double z = object.hasRenderPosition ? object.renderLevel : object.level;
        for (const auto& impact : *context.impactActors) {
            if (impact.damage <= 0 || impact.remainingTicks <= 0 || impact.totalTicks <= 0
                || !runtimeVehicleBodyHasBlastIntent(impact)) continue;
            const auto* wh = context.rules->findWarheadType(impact.warheadId);
            if (wh == nullptr || wh->parasite || wh->mindControl || wh->temporal
                || wh->radiation || wh->psychicDamage || wh->electricAssault
                || (!wh->rocker && wh->cellSpread <= 0.0)) continue;
            const double radius = std::clamp(std::max(wh->cellSpread, 0.5) + 1.25, 1.0, 6.0);
            const double dx = x - impact.x, dy = y - impact.y;
            const double distance = std::sqrt(dx * dx + dy * dy + (z - impact.level) * (z - impact.level));
            const auto impulse = runtimeVehicleBlastMotion(
                (impact.totalTicks - impact.remainingTicks) / hz,
                std::min(0.9, impact.totalTicks / hz), dx, dy, distance, radius,
                impact.damage, unit->weight,
                runtimeVehicleBlastRockingCoefficient(*wh, *context.rules));
            pose.pitch += impulse.pitch; pose.roll += impulse.roll; pose.height += impulse.height;
        }
    }
    pose.pitch = std::clamp(pose.pitch, -0.31, 0.31);
    pose.roll = std::clamp(pose.roll, -0.31, 0.31);
    pose.height = std::clamp(pose.height, 0.0, 0.22);
    return pose;
}

bool runRuntimeVehicleBodyPoseContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    const auto fail = [&](const std::string& message) {
        error = "vehicle pose sources: " + message;
        return false;
    };
    const auto equal = [](const RuntimeVehicleBodyPose& a, const RuntimeVehicleBodyPose& b) {
        return a.pitch == b.pitch && a.roll == b.roll && a.height == b.height;
    };
    std::vector<RuntimeImpactRenderActor> impacts(512);
    const char* warheads[] = {"V3WH", "DMISLWH", "Parasite", "NUKE", "missing-warhead"};
    const char* intents[] = {"warheadImpact", "bombImpact", "deathImpact",
        "unitDestruction", "superWeaponEffect", "smoke"};
    for (std::size_t index = 0; index < impacts.size(); ++index) {
        auto& impact = impacts[index];
        impact.warheadId = warheads[index % 5];
        impact.renderIntent = intents[index % 6];
        impact.damage = index % 7 == 0 ? 0 : 20 + static_cast<int>(index % 600);
        impact.totalTicks = index % 13 == 0 ? 0 : 30;
        impact.remainingTicks = 32 - static_cast<int>(index % 35);
        impact.x = static_cast<double>(index % 19) * 0.9;
        impact.y = static_cast<double>((index * 7) % 17) * 1.1;
        impact.level = static_cast<int>(index % 3);
    }
    std::vector<SpecialMechanismRenderSnapshot> mechanisms(256);
    for (std::size_t index = 0; index < mechanisms.size(); ++index) {
        auto& mechanism = mechanisms[index];
        mechanism.kind = index % 5 == 0 ? "mindControl" : "parasite";
        mechanism.targetObjectId = static_cast<int>(index % 64);
        mechanism.resourceId = index % 3 == 0 ? "DMISLWH" : index % 3 == 1 ? "Parasite" : "missing";
        mechanism.phase = index % 7 == 0 ? "organic" : "mechanical";
        mechanism.progress = static_cast<int>(index) - 20;
        mechanism.intensity = index % 11 == 0 ? 0 : 50;
        mechanism.requiredProgress = 120;
        mechanism.expireTick = 500 - static_cast<int>(index % 150);
    }
    RuntimeMapRenderContext probe = context;
    probe.impactActors = &impacts;
    probe.specialMechanismSnapshots = &mechanisms;
    probe.fixedTickHz = 30;
    probe.animationTick = 400;
    std::vector<WorldObjectInstance> objects(4096);
    const char* types[] = {"MTNK", "HTNK", "APOC", "MIND", "missing-vehicle"};
    for (std::size_t index = 0; index < objects.size(); ++index) {
        auto& object = objects[index];
        object.instanceId = static_cast<int>(index % 96);
        object.kind = index % 47 == 0 ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        object.type = types[index % 5];
        object.alive = index % 61 != 0;
        object.health = index % 41 == 0 ? 0 : 100;
        object.dockedToObjectId = index % 37 == 0 ? 100 : -1;
        object.submerged = index % 43 == 0;
        object.cloaked = index % 53 == 0;
        object.x = static_cast<int>(index % 21);
        object.y = static_cast<int>((index * 7) % 23);
        object.level = static_cast<int>(index % 3);
        object.hasRenderPosition = index % 2 != 0;
        object.renderX = object.x + 0.25;
        object.renderY = object.y - 0.3;
        object.renderLevel = object.level + 0.1;
    }
    std::vector<RuntimeVehicleBodyPose> reference;
    reference.reserve(objects.size());
    for (const auto& object : objects)
        reference.push_back(runtimeVehicleBodyPoseReferenceForContract(object, probe));

    std::size_t filteredImpacts = 0;
    auto* outer = RuntimeVehicleBodyPoseRenderScope::active;
    {
        RuntimeVehicleBodyPoseRenderScope scope(probe);
        if (scope.prepared) return fail("unused draw eagerly scanned its inputs");
        for (std::size_t index = 0; index < objects.size(); ++index) {
            if (!equal(reference[index], runtimeVehicleBodyPoseForRender(objects[index], probe)))
                return fail("old/new pose differs at object " + std::to_string(index));
        }
        filteredImpacts = scope.sources.impacts.size();
        if (!scope.prepared || filteredImpacts == 0 || filteredImpacts >= impacts.size())
            return fail("inactive effects were not removed before per-object evaluation");
        RuntimeMapRenderContext alternate = probe;
        alternate.fixedTickHz = 60;
        {
            RuntimeVehicleBodyPoseRenderScope nested(alternate);
            for (const auto& object : objects) {
                if (!equal(runtimeVehicleBodyPoseReferenceForContract(object, alternate),
                        runtimeVehicleBodyPoseForRender(object, alternate)))
                    return fail("nested draw changed time-base behavior");
            }
        }
        if (RuntimeVehicleBodyPoseRenderScope::active != &scope)
            return fail("nested draw did not restore its caller");
        // A direct helper call with unrelated inputs must not borrow the
        // active draw's sources merely because it runs on the same thread.
        alternate.impactActors = nullptr;
        for (int index = 0; index < 128; ++index) {
            if (!equal(runtimeVehicleBodyPoseReferenceForContract(objects[index], alternate),
                    runtimeVehicleBodyPoseForRender(objects[index], alternate)))
                return fail("foreign render context reused stale sources");
        }
    }
    if (RuntimeVehicleBodyPoseRenderScope::active != outer)
        return fail("draw scope leaked into subsequent frames");
    // Reusing the same input vector next frame must observe changed contents.
    mechanisms[1].progress += 100;
    impacts[1].damage += 500;
    {
        RuntimeVehicleBodyPoseRenderScope nextFrame(probe);
        for (int index = 0; index < 128; ++index) {
            if (!equal(runtimeVehicleBodyPoseReferenceForContract(objects[index], probe),
                    runtimeVehicleBodyPoseForRender(objects[index], probe)))
                return fail("next frame reused stale vector contents");
        }
    }
    std::cout << "[runtime-vehicle-body-sources] exactPoses=8448 impacts=" << impacts.size()
        << " preparedImpacts=" << filteredImpacts
        << " mechanisms=" << mechanisms.size()
        << " nestedScopes=yes nextFrameRefresh=yes foreignContextFallback=yes\n";
    return true;
}
