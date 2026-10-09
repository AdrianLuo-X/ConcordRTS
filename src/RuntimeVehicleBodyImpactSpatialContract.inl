bool runRuntimeVehicleBodyImpactSpatialContractCheck(std::string& error) {
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromString("[VehicleTypes]\n0=__ROCK_LIGHT__\n1=__ROCK_HEAVY__\n"
            "[Warheads]\n0=__ROCK_AREA__\n1=__ROCK_DIRECT__\n"
            "[__ROCK_LIGHT__]\nWeight=1.5\nStrength=100\n"
            "[__ROCK_HEAVY__]\nWeight=6\nStrength=100\n"
            "[__ROCK_AREA__]\nCellSpread=2.75\n"
            "[__ROCK_DIRECT__]\nCellSpread=6\nRocker=yes\n")
        || !rules.loadFromIni(ini)) {
        error = "vehicle impact spatial fixture failed"; return false;
    }
    RuntimeMapRenderContext context;
    context.rules = &rules; context.fixedTickHz = 30; context.animationTick = 400;
    std::size_t evaluatedPairs = 0, allPairs = 0, exactPoses = 0;
    for (const int count : {128, 256}) {
        std::vector<RuntimeImpactRenderActor> impacts(static_cast<std::size_t>(count * 2));
        for (std::size_t index = 0; index < impacts.size(); ++index) {
            auto& impact = impacts[index];
            impact.actorId = static_cast<int>(index);
            impact.warheadId = index % 3 ? "__ROCK_AREA__" : "__ROCK_DIRECT__";
            impact.renderIntent = index % 13 == 0 ? "smoke" : "warheadImpact";
            impact.damage = 15 + static_cast<int>(index % 71);
            impact.totalTicks = 30; impact.remainingTicks = 30 - static_cast<int>(index % 31);
            // Near blasts overlap densely; farther rows exercise broad-phase
            // rejection while retained contributions remain in source order.
            impact.x = static_cast<double>(index % 24) * 0.75 - 9.0;
            impact.y = static_cast<double>(index / 24) * 1.25 - 10.0;
            impact.level = static_cast<double>(index % 5) * 0.25;
        }
        std::vector<SpecialMechanismRenderSnapshot> mechanisms(2);
        for (int index = 0; index < 2; ++index) {
            auto& mechanism = mechanisms[index];
            mechanism.kind = "parasite"; mechanism.phase = "mechanical";
            mechanism.targetObjectId = index; mechanism.resourceId = "__ROCK_DIRECT__";
            mechanism.intensity = 50; mechanism.progress = 30;
            mechanism.requiredProgress = 120; mechanism.expireTick = 517;
        }
        context.impactActors = &impacts; context.specialMechanismSnapshots = &mechanisms;
        std::vector<WorldObjectInstance> objects(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            auto& object = objects[index];
            object.instanceId = index; object.kind = WorldObjectKind::Unit;
            object.type = index % 2 ? "__ROCK_LIGHT__" : "__ROCK_HEAVY__";
            object.health = 100; object.alive = true; object.hasRenderPosition = true;
            object.renderX = (index % 16) * 1.125 - 9.0;
            object.renderY = (index / 16) * 1.25 - 10.0;
            object.renderLevel = (index % 3) * 0.125;
        }
        const auto same = [](double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); };
        const auto verify = [&]() {
            RuntimeVehicleBodyPoseRenderScope scope(context);
            for (const auto& object : objects) {
                const auto expected = runtimeVehicleBodyPoseReferenceForContract(object, context);
                const auto actual = runtimeVehicleBodyPoseForRender(object, context);
                if (!same(actual.pitch, expected.pitch) || !same(actual.roll, expected.roll)
                    || !same(actual.height, expected.height)) {
                    error = "vehicle impact spatial pose differs: count=" + std::to_string(count)
                        + " object=" + std::to_string(object.instanceId);
                    return false;
                }
                const auto* candidates = scope.sources.candidatesFor(
                    object.renderX, object.renderY, object.renderLevel);
                evaluatedPairs += candidates ? candidates->size() : scope.sources.impacts.size();
                allPairs += scope.sources.impacts.size();
                ++exactPoses;
            }
            return true;
        };
        if (!verify()) return false;
        // Scope rebuilding must observe moving blasts and changed heights.
        for (auto& impact : impacts) { impact.x += 16; impact.y -= 8; --impact.remainingTicks; }
        if (!verify()) return false;
        // Invalid coordinates follow the former full-loop propagation, never
        // disappear just because their finite XY position is outside a bucket.
        impacts[1].level = std::numeric_limits<double>::quiet_NaN();
        impacts[2].x = std::numeric_limits<double>::infinity();
        objects[0].renderLevel = std::numeric_limits<double>::quiet_NaN();
        objects[1].renderX = std::numeric_limits<double>::quiet_NaN();
        if (!verify()) return false;
    }
    if (!runtimeMovementReferenceForRender() && evaluatedPairs >= allPairs) {
        error = "vehicle impact spatial broad phase removed no distant pairs"; return false;
    }
    std::cout << "[runtime-vehicle-impact-spatial] exactPoses=" << exactPoses
        << " candidates=" << evaluatedPairs << "/" << allPairs << '\n';
    return true;
}
