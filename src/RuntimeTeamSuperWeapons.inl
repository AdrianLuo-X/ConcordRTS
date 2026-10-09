RuntimeTeamStepDisposition issueRuntimeTeamOwnedSuperWeapon(RuntimeWindowState& state,
    RuntimeTeamMissionState& mission, bool chronosphere, RuntimeCellPoint destination) {
    const auto owner = runtimeTeamMissionOwner(state, mission);
    auto center = runtimeTeamMissionCenter(state, mission);
    if (center.x < 0 || center.y < 0) return RuntimeTeamStepDisposition::Complete;
    if (!state.config.superweapons) return RuntimeTeamStepDisposition::Complete;
    ensureRuntimeSuperWeaponsInitialized(state);
    if (!state.superWeaponsInitialized) return RuntimeTeamStepDisposition::Waiting;
    std::string weaponId;
    for (const auto& rule : state.rules.getSuperWeaponTypes()) {
        const auto identity = toLowerAsciiLocal(rule.id + " " + rule.type);
        if (identity.find(chronosphere ? "chrono" : "ironcurtain") == std::string::npos) continue;
        const auto* snapshot = state.superWeapons.findSidebarSnapshot(owner, rule.id);
        if (snapshot && snapshot->state == SuperWeaponRuntimeState::Ready) { weaponId = rule.id; break; }
    }
    if (weaponId.empty()) {
        mission.status = chronosphere ? "waiting for charged Chronosphere" : "waiting for charged Iron Curtain";
        return RuntimeTeamStepDisposition::Waiting;
    }
    center = runtimeTeamSuperWeaponCenter(state, mission, *state.rules.findSuperWeaponType(weaponId));
    if (chronosphere) {
        for (int objectId : mission.objectIds) {
            const auto* actor = findObjectById(state, objectId);
            if (!actor) continue;
            const auto cell = findRuntimePassableCellNear(state, *actor, destination, 6);
            if (cell) { destination = *cell; break; }
        }
    }
    RuntimeMultiplayerGameplayFireSuperWeaponCommand command;
    command.superWeaponId = weaponId;
    RuntimeMultiplayerGameplaySuperWeaponTarget first;
    first.cell = {center.x, center.y};
    command.targets.push_back(first);
    if (chronosphere) {
        RuntimeMultiplayerGameplaySuperWeaponTarget second;
        second.cell = {destination.x, destination.y}; command.targets.push_back(second);
    }
    // Validate without side effects, using the same charged/source/power and
    // target rules as a player's launch. No grant or trigger-force-fire path.
    if (!runtimeGameplaySuperWeaponReadyForCommand(state, owner, command))
        return RuntimeTeamStepDisposition::Waiting;
    auto context = runtimeSuperWeaponContext(state);
    context.emit = runtimeSuperWeaponEventSink(state);
    const SuperWeaponTarget source{{center.x, center.y}, -1};
    bool fired = false;
    if (chronosphere) {
        fired = state.superWeapons.submitTwoStageTarget(owner, weaponId, source,
            SuperWeaponTarget{{destination.x, destination.y}, -1}, context);
    } else if (state.superWeapons.beginTargetSelection(owner, weaponId, context)) {
        fired = state.superWeapons.submitTarget(owner, weaponId, source, context);
    }
    (void)drainRuntimeObjectExitQueue(state);
    if (fired) mission.status = chronosphere ? "Chronosphere launched" : "Iron Curtain launched";
    return fired ? RuntimeTeamStepDisposition::Complete : RuntimeTeamStepDisposition::Waiting;
}
